#include "rtc_encoded_video_frame_encoder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "api/video/encoded_image.h"
#include "api/video_codecs/video_codec.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/time_utils.h"

namespace libwebrtc {
namespace {

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

webrtc::VideoCodecType WebRtcCodecType(RTCEncodedVideoCodec codec) {
  switch (codec) {
    case RTCEncodedVideoCodec::kAV1:
      return webrtc::kVideoCodecAV1;
    case RTCEncodedVideoCodec::kVP8:
      return webrtc::kVideoCodecVP8;
    case RTCEncodedVideoCodec::kVP9:
      return webrtc::kVideoCodecVP9;
    case RTCEncodedVideoCodec::kH264:
      return webrtc::kVideoCodecH264;
    case RTCEncodedVideoCodec::kH265:
      return webrtc::kVideoCodecH265;
    case RTCEncodedVideoCodec::kUnknown:
    default:
      return webrtc::kVideoCodecGeneric;
  }
}

bool FormatMatchesCodec(const webrtc::SdpVideoFormat& format,
                        RTCEncodedVideoCodec codec) {
  const char* name = CodecSdpName(codec);
  return name[0] != '\0' && format.name == name;
}

class ExternalEncodedVideoFrameEncoder final : public webrtc::VideoEncoder {
 public:
  explicit ExternalEncodedVideoFrameEncoder(
      scoped_refptr<ExternalEncodedVideoFrameSenderImpl> sender)
      : sender_(std::move(sender)) {}

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const Settings& settings) override {
    (void)codec_settings;
    (void)settings;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    if (sender_) {
      sender_->SetEncoderCallback(callback);
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override {
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
    info.enable_cpu_overuse_detection = false;
    info.scaling_settings = ScalingSettings::kOff;
    return info;
  }

 private:
  scoped_refptr<ExternalEncodedVideoFrameSenderImpl> sender_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
};

}  // namespace

ExternalEncodedVideoFrameSenderImpl::ExternalEncodedVideoFrameSenderImpl(
    RTCEncodedVideoCodec codec,
    uint32_t width,
    uint32_t height,
    uint32_t frame_rate,
    uint32_t bitrate_bps)
    : codec_(codec),
      width_(width),
      height_(height),
      frame_rate_(frame_rate ? frame_rate : 30),
      bitrate_bps_(bitrate_bps) {}

bool ExternalEncodedVideoFrameSenderImpl::SubmitEncodedVideoFrame(
    const RTCEncodedVideoFrame& frame) {
  if (!frame.data || frame.size == 0 || frame.codec != codec_) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!callback_) {
    return false;
  }

  webrtc::scoped_refptr<webrtc::EncodedImageBuffer> buffer =
      webrtc::EncodedImageBuffer::Create(frame.data, frame.size);
  if (!buffer) {
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
  image.set_end_of_temporal_unit(true);
  image.SetRetransmissionAllowed(true);
  if (frame.tracking_frame_id) {
    image.SetVideoFrameTrackingId(frame.tracking_frame_id);
  }

  webrtc::CodecSpecificInfo codec_info{};
  codec_info.codecType = WebRtcCodecType(codec_);
  codec_info.end_of_picture = true;

  wants_key_frame_ = false;
  return callback_->OnEncodedImage(image, &codec_info).error ==
         webrtc::EncodedImageCallback::Result::OK;
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
}

void ExternalEncodedVideoFrameSenderImpl::ClearEncoderCallback(
    webrtc::EncodedImageCallback* callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!callback || callback_ == callback) {
    callback_ = nullptr;
  }
}

void ExternalEncodedVideoFrameSenderImpl::RequestKeyFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  wants_key_frame_ = true;
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
    CodecSupport support;
    support.is_supported = true;
    support.is_power_efficient = true;
    return support;
  }
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
    return std::make_unique<ExternalEncodedVideoFrameEncoder>(sender);
  }
  return inner_ ? inner_->Create(env, format) : nullptr;
}

std::unique_ptr<webrtc::VideoEncoderFactory::EncoderSelectorInterface>
ExternalEncodedVideoFrameEncoderFactory::GetEncoderSelector() const {
  return inner_ ? inner_->GetEncoderSelector() : nullptr;
}

}  // namespace libwebrtc
