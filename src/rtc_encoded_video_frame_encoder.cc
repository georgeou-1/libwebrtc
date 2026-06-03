#include "rtc_encoded_video_frame_encoder.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

#include "api/video/encoded_image.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/time_utils.h"

namespace libwebrtc {
namespace {

std::atomic<uint64_t> g_external_encoded_sender_id{1};
std::atomic<uint64_t> g_external_encoded_encoder_id{1};

const char* CodecSdpName(RTCEncodedVideoCodec codec) {
  switch (codec) {
    case RTCEncodedVideoCodec::kAV1:
      return "AV1";
    case RTCEncodedVideoCodec::kVP8:
      return "VP8";
    case RTCEncodedVideoCodec::kVP9:
      return "VP9";
    case RTCEncodedVideoCodec::kH264:
      return "H264";
    case RTCEncodedVideoCodec::kH265:
      return "H265";
    case RTCEncodedVideoCodec::kUnknown:
    default:
      return "";
  }
}

bool FormatMatchesCodec(const webrtc::SdpVideoFormat& format,
                        RTCEncodedVideoCodec codec) {
  const char* name = CodecSdpName(codec);
  return name[0] != '\0' && format.name == name;
}

void LogExternalEncodedEvent(const char* event, const std::string& fields) {
  std::cerr << "{\"event\":\"libwebrtc_external_encoded_" << event << "\"";
  if (!fields.empty()) {
    std::cerr << "," << fields;
  }
  std::cerr << "}" << std::endl;
}

std::string FormatJson(const webrtc::SdpVideoFormat& format) {
  return "\"format\":\"" + format.name + "\"";
}

class ExternalEncodedVideoFrameEncoder final : public webrtc::VideoEncoder {
 public:
  explicit ExternalEncodedVideoFrameEncoder(
      scoped_refptr<ExternalEncodedVideoFrameSenderImpl> sender)
      : sender_(std::move(sender)),
        debug_id_(g_external_encoded_encoder_id.fetch_add(1)) {
    LogExternalEncodedEvent(
        "encoder_created",
        "\"encoder_id\":" + std::to_string(debug_id_) +
            ",\"sender_id\":" +
            std::to_string(sender_ ? sender_->debug_id() : 0));
  }

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const Settings& settings) override {
    (void)settings;
    const char* codec_name =
        codec_settings
            ? (codec_settings->codecType == webrtc::kVideoCodecAV1 ? "AV1"
                                                                    : "other")
            : "null";
    LogExternalEncodedEvent(
        "encoder_init",
        "\"encoder_id\":" + std::to_string(debug_id_) +
            ",\"sender_id\":" +
            std::to_string(sender_ ? sender_->debug_id() : 0) +
            ",\"codec\":\"" + codec_name + "\"");
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    if (sender_) {
      sender_->SetEncoderCallback(callback);
    }
    LogExternalEncodedEvent(
        "encoder_register_callback",
        "\"encoder_id\":" + std::to_string(debug_id_) +
            ",\"sender_id\":" +
            std::to_string(sender_ ? sender_->debug_id() : 0) +
            ",\"callback_present\":" + std::string(callback ? "true" : "false"));
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override {
    LogExternalEncodedEvent(
        "encoder_release",
        "\"encoder_id\":" + std::to_string(debug_id_) +
            ",\"sender_id\":" +
            std::to_string(sender_ ? sender_->debug_id() : 0) +
            ",\"callback_present\":" + std::string(callback_ ? "true" : "false"));
    if (sender_) {
      sender_->ClearEncoderCallback(callback_);
    }
    callback_ = nullptr;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types) override {
    (void)frame;
    if (sender_ && frame_types) {
      for (webrtc::VideoFrameType type : *frame_types) {
        if (type == webrtc::VideoFrameType::kVideoFrameKey) {
          sender_->RequestKeyFrame();
          LogExternalEncodedEvent(
              "encoder_keyframe_requested",
              "\"encoder_id\":" + std::to_string(debug_id_) +
                  ",\"sender_id\":" + std::to_string(sender_->debug_id()));
          break;
        }
      }
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }

  void SetRates(const RateControlParameters& parameters) override {
    (void)parameters;
  }

  EncoderInfo GetEncoderInfo() const override {
    EncoderInfo info;
    info.implementation_name = "mcloud-external-encoded-video";
    info.is_hardware_accelerated = true;
    info.has_trusted_rate_controller = true;
    info.supports_native_handle = true;
    info.scaling_settings = ScalingSettings::kOff;
    return info;
  }

 private:
  scoped_refptr<ExternalEncodedVideoFrameSenderImpl> sender_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  uint64_t debug_id_ = 0;
};

}  // namespace

ExternalEncodedVideoFrameSenderImpl::ExternalEncodedVideoFrameSenderImpl(
    RTCEncodedVideoCodec codec,
    uint32_t width,
    uint32_t height,
    uint32_t frame_rate,
    uint32_t bitrate_bps)
    : codec_(codec),
      debug_id_(g_external_encoded_sender_id.fetch_add(1)),
      width_(width),
      height_(height),
      frame_rate_(frame_rate ? frame_rate : 30),
      bitrate_bps_(bitrate_bps) {
  LogExternalEncodedEvent(
      "sender_created",
      "\"sender_id\":" + std::to_string(debug_id_) +
          ",\"codec\":\"" + std::string(CodecSdpName(codec_)) + "\"" +
          ",\"width\":" + std::to_string(width_) +
          ",\"height\":" + std::to_string(height_) +
          ",\"fps\":" + std::to_string(frame_rate_) +
          ",\"bitrate_bps\":" + std::to_string(bitrate_bps_));
}

bool ExternalEncodedVideoFrameSenderImpl::SubmitEncodedVideoFrame(
    const RTCEncodedVideoFrame& frame) {
  if (!frame.data || frame.size == 0 || frame.codec != codec_) {
    LogExternalEncodedEvent(
        "sender_submit_rejected",
        "\"sender_id\":" + std::to_string(debug_id_) +
            ",\"reason\":\"invalid-frame-or-codec\"" +
            ",\"bytes\":" + std::to_string(frame.size) +
            ",\"frame_codec\":\"" + std::string(CodecSdpName(frame.codec)) +
            "\"" + ",\"sender_codec\":\"" +
            std::string(CodecSdpName(codec_)) + "\"");
    return false;
  }

  webrtc::EncodedImageCallback* callback = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback = callback_;
  }
  if (!callback) {
    LogExternalEncodedEvent(
        "sender_submit_rejected",
        "\"sender_id\":" + std::to_string(debug_id_) +
            ",\"reason\":\"callback-missing\"" +
            ",\"bytes\":" + std::to_string(frame.size) +
            ",\"keyframe\":" + std::string(frame.key_frame ? "true" : "false") +
            ",\"rtp_timestamp\":" + std::to_string(frame.rtp_timestamp));
    return false;
  }

  webrtc::scoped_refptr<webrtc::EncodedImageBuffer> buffer =
      webrtc::EncodedImageBuffer::Create(frame.data, frame.size);
  if (!buffer) {
    LogExternalEncodedEvent(
        "sender_submit_rejected",
        "\"sender_id\":" + std::to_string(debug_id_) +
            ",\"reason\":\"buffer-create-failed\"" +
            ",\"bytes\":" + std::to_string(frame.size));
    return false;
  }

  const int64_t now_ms = webrtc::TimeMillis();
  webrtc::EncodedImage image;
  image.SetEncodedData(buffer);
  image.SetRtpTimestamp(frame.rtp_timestamp);
  image._encodedWidth = frame.width ? frame.width : width_;
  image._encodedHeight = frame.height ? frame.height : height_;
  image.capture_time_ms_ = frame.capture_time_ms ? frame.capture_time_ms : now_ms;
  image.ntp_time_ms_ = frame.ntp_time_ms ? frame.ntp_time_ms : now_ms;
  image.SetFrameType(frame.key_frame
                         ? webrtc::VideoFrameType::kVideoFrameKey
                         : webrtc::VideoFrameType::kVideoFrameDelta);
  image.SetRetransmissionAllowed(true);
  if (frame.tracking_frame_id) {
    image.SetVideoFrameTrackingId(frame.tracking_frame_id);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    wants_key_frame_ = false;
  }
  webrtc::EncodedImageCallback::Result result =
      callback->OnEncodedImage(image, nullptr);
  const bool ok = result.error == webrtc::EncodedImageCallback::Result::OK;
  LogExternalEncodedEvent(
      "sender_submit_result",
      "\"sender_id\":" + std::to_string(debug_id_) +
          ",\"bytes\":" + std::to_string(frame.size) +
          ",\"keyframe\":" + std::string(frame.key_frame ? "true" : "false") +
          ",\"rtp_timestamp\":" + std::to_string(frame.rtp_timestamp) +
          ",\"ok\":" + std::string(ok ? "true" : "false") +
          ",\"error\":" + std::to_string(static_cast<int>(result.error)));
  return ok;
}

bool ExternalEncodedVideoFrameSenderImpl::WantsKeyFrame() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return wants_key_frame_;
}

bool ExternalEncodedVideoFrameSenderImpl::Matches(
    const webrtc::SdpVideoFormat& format) const {
  return FormatMatchesCodec(format, codec_);
}

void ExternalEncodedVideoFrameSenderImpl::SetEncoderCallback(
    webrtc::EncodedImageCallback* callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = callback;
  wants_key_frame_ = true;
  LogExternalEncodedEvent(
      "sender_callback_set",
      "\"sender_id\":" + std::to_string(debug_id_) +
          ",\"callback_present\":" + std::string(callback ? "true" : "false"));
}

void ExternalEncodedVideoFrameSenderImpl::ClearEncoderCallback(
    webrtc::EncodedImageCallback* callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!callback || callback_ == callback) {
    callback_ = nullptr;
  }
  LogExternalEncodedEvent(
      "sender_callback_cleared",
      "\"sender_id\":" + std::to_string(debug_id_) +
          ",\"callback_present\":" + std::string(callback_ ? "true" : "false"));
}

void ExternalEncodedVideoFrameSenderImpl::RequestKeyFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  wants_key_frame_ = true;
  LogExternalEncodedEvent(
      "sender_keyframe_requested",
      "\"sender_id\":" + std::to_string(debug_id_));
}

ExternalEncodedVideoFrameEncoderFactory::
    ExternalEncodedVideoFrameEncoderFactory(
        std::unique_ptr<webrtc::VideoEncoderFactory> inner,
        scoped_refptr<ExternalEncodedVideoFrameSenderImpl>* sender)
    : inner_(std::move(inner)), sender_(sender) {}

std::vector<webrtc::SdpVideoFormat>
ExternalEncodedVideoFrameEncoderFactory::GetSupportedFormats() const {
  return inner_ ? inner_->GetSupportedFormats()
                : std::vector<webrtc::SdpVideoFormat>();
}

std::vector<webrtc::SdpVideoFormat>
ExternalEncodedVideoFrameEncoderFactory::GetImplementations() const {
  return inner_ ? inner_->GetImplementations()
                : std::vector<webrtc::SdpVideoFormat>();
}

webrtc::VideoEncoderFactory::CodecSupport
ExternalEncodedVideoFrameEncoderFactory::QueryCodecSupport(
    const webrtc::SdpVideoFormat& format,
    std::optional<std::string> scalability_mode) const {
  scoped_refptr<ExternalEncodedVideoFrameSenderImpl> sender =
      sender_ ? *sender_ : nullptr;
  if (sender && sender->Matches(format)) {
    LogExternalEncodedEvent(
        "factory_query_match",
        FormatJson(format) + ",\"sender_id\":" +
            std::to_string(sender->debug_id()) +
            ",\"supported\":true");
    CodecSupport support;
    support.is_supported = true;
    support.is_power_efficient = true;
    return support;
  }
  LogExternalEncodedEvent(
      "factory_query_passthrough",
      FormatJson(format) + ",\"sender_id\":" +
          std::to_string(sender ? sender->debug_id() : 0));
  return inner_ ? inner_->QueryCodecSupport(format, scalability_mode)
                : CodecSupport{};
}

std::unique_ptr<webrtc::VideoEncoder>
ExternalEncodedVideoFrameEncoderFactory::Create(
    const webrtc::Environment& env,
    const webrtc::SdpVideoFormat& format) {
  scoped_refptr<ExternalEncodedVideoFrameSenderImpl> sender =
      sender_ ? *sender_ : nullptr;
  if (sender && sender->Matches(format)) {
    LogExternalEncodedEvent(
        "factory_create_match",
        FormatJson(format) + ",\"sender_id\":" +
            std::to_string(sender->debug_id()));
    return std::make_unique<ExternalEncodedVideoFrameEncoder>(sender);
  }
  LogExternalEncodedEvent(
      "factory_create_passthrough",
      FormatJson(format) + ",\"sender_id\":" +
          std::to_string(sender ? sender->debug_id() : 0));
  return inner_ ? inner_->Create(env, format) : nullptr;
}

std::unique_ptr<webrtc::VideoEncoderFactory::EncoderSelectorInterface>
ExternalEncodedVideoFrameEncoderFactory::GetEncoderSelector() const {
  return inner_ ? inner_->GetEncoderSelector() : nullptr;
}

}  // namespace libwebrtc
