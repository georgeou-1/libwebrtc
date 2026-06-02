#include "rtc_encoded_video_frame_decoder.h"

#include <algorithm>

#include "api/video/encoded_image.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_error_codes.h"

namespace libwebrtc {
namespace {

RTCEncodedVideoCodec CodecFromSdpName(const std::string& name) {
  if (name == "AV1") {
    return RTCEncodedVideoCodec::kAV1;
  }
  if (name == "VP8") {
    return RTCEncodedVideoCodec::kVP8;
  }
  if (name == "VP9") {
    return RTCEncodedVideoCodec::kVP9;
  }
  if (name == "H264") {
    return RTCEncodedVideoCodec::kH264;
  }
  if (name == "H265") {
    return RTCEncodedVideoCodec::kH265;
  }
  return RTCEncodedVideoCodec::kUnknown;
}

class EncodedVideoFrameForwardingDecoder final : public webrtc::VideoDecoder {
 public:
  EncodedVideoFrameForwardingDecoder(
      std::unique_ptr<webrtc::VideoDecoder> inner,
      RTCEncodedVideoFrameReceiver** receiver,
      RTCEncodedVideoCodec codec)
      : inner_(std::move(inner)), receiver_(receiver), codec_(codec) {}

  bool Configure(const Settings& settings) override {
    return inner_ && inner_->Configure(settings);
  }

  int32_t Decode(const webrtc::EncodedImage& input_image,
                 bool missing_frames,
                 int64_t render_time_ms = -1) override {
    Forward(input_image);
    return inner_ ? inner_->Decode(input_image, missing_frames, render_time_ms)
                  : WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback) override {
    return inner_ ? inner_->RegisterDecodeCompleteCallback(callback)
                  : WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  int32_t Release() override {
    return inner_ ? inner_->Release() : WEBRTC_VIDEO_CODEC_OK;
  }

  const char* ImplementationName() const override {
    return inner_ ? inner_->ImplementationName()
                  : "encoded-frame-forwarding-decoder";
  }

 private:
  void Forward(const webrtc::EncodedImage& input_image) {
    RTCEncodedVideoFrameReceiver* receiver =
        receiver_ ? *receiver_ : nullptr;
    if (!receiver) {
      return;
    }
    webrtc::scoped_refptr<webrtc::EncodedImageBufferInterface> data =
        input_image.GetEncodedData();
    const size_t bytes = data
        ? std::min<size_t>(input_image.size(), data->size())
        : 0;
    if (!data || !data->data() || bytes == 0) {
      return;
    }

    RTCEncodedVideoFrame frame;
    frame.codec = codec_;
    frame.data = data->data();
    frame.size = bytes;
    frame.width = input_image._encodedWidth;
    frame.height = input_image._encodedHeight;
    frame.rtp_timestamp = input_image.RtpTimestamp();
    frame.duration_90k = 3000;
    frame.frame_sequence =
        frame_sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (input_image.VideoFrameTrackingId()) {
      frame.tracking_frame_id =
          static_cast<uint64_t>(*input_image.VideoFrameTrackingId());
    }
    frame.key_frame =
        input_image.FrameType() == webrtc::VideoFrameType::kVideoFrameKey;
    receiver->OnEncodedVideoFrame(frame);
  }

  std::unique_ptr<webrtc::VideoDecoder> inner_;
  RTCEncodedVideoFrameReceiver** receiver_ = nullptr;
  RTCEncodedVideoCodec codec_ = RTCEncodedVideoCodec::kUnknown;
  std::atomic<uint64_t> frame_sequence_{0};
};

}  // namespace

EncodedVideoFrameForwardingDecoderFactory::
    EncodedVideoFrameForwardingDecoderFactory(
        std::unique_ptr<webrtc::VideoDecoderFactory> inner,
        RTCEncodedVideoFrameReceiver** receiver)
    : inner_(std::move(inner)), receiver_(receiver) {}

std::vector<webrtc::SdpVideoFormat>
EncodedVideoFrameForwardingDecoderFactory::GetSupportedFormats() const {
  return inner_ ? inner_->GetSupportedFormats()
                : std::vector<webrtc::SdpVideoFormat>();
}

std::unique_ptr<webrtc::VideoDecoder>
EncodedVideoFrameForwardingDecoderFactory::Create(
    const webrtc::SdpVideoFormat& format) {
  std::unique_ptr<webrtc::VideoDecoder> decoder =
      inner_ ? inner_->Create(format) : nullptr;
  if (!decoder) {
    return nullptr;
  }
  return std::make_unique<EncodedVideoFrameForwardingDecoder>(
      std::move(decoder), receiver_, CodecFromSdpName(format.name));
}

}  // namespace libwebrtc
