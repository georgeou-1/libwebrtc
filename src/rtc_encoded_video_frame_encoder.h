#ifndef LIB_WEBRTC_RTC_ENCODED_VIDEO_FRAME_ENCODER_HXX
#define LIB_WEBRTC_RTC_ENCODED_VIDEO_FRAME_ENCODER_HXX

#include <memory>
#include <mutex>
#include <optional>
#include <cstdint>
#include <vector>

#include "api/environment/environment.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "base/refcountedobject.h"
#include "rtc_encoded_video_frame.h"

namespace libwebrtc {

class ExternalEncodedVideoFrameSenderImpl
    : public RTCEncodedVideoFrameSender {
 public:
  ExternalEncodedVideoFrameSenderImpl(
      RTCEncodedVideoCodec codec,
      uint32_t width,
      uint32_t height,
      uint32_t frame_rate,
      uint32_t bitrate_bps);

  bool SubmitEncodedVideoFrame(const RTCEncodedVideoFrame& frame) override;
  bool WantsKeyFrame() const override;

  bool Matches(const webrtc::SdpVideoFormat& format) const;
  void SetEncoderCallback(webrtc::EncodedImageCallback* callback);
  void ClearEncoderCallback(webrtc::EncodedImageCallback* callback);
  void RequestKeyFrame();

  uint64_t debug_id() const { return debug_id_; }
  RTCEncodedVideoCodec codec() const { return codec_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  uint32_t frame_rate() const { return frame_rate_; }
  uint32_t bitrate_bps() const { return bitrate_bps_; }

 private:
  RTCEncodedVideoCodec codec_ = RTCEncodedVideoCodec::kUnknown;
  uint64_t debug_id_ = 0;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t frame_rate_ = 30;
  uint32_t bitrate_bps_ = 1000000;
  mutable std::mutex mutex_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  bool wants_key_frame_ = true;
};

class ExternalEncodedVideoFrameEncoderFactory final
    : public webrtc::VideoEncoderFactory {
 public:
  ExternalEncodedVideoFrameEncoderFactory(
      std::unique_ptr<webrtc::VideoEncoderFactory> inner,
      scoped_refptr<ExternalEncodedVideoFrameSenderImpl>* sender);

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
  std::vector<webrtc::SdpVideoFormat> GetImplementations() const override;
  CodecSupport QueryCodecSupport(
      const webrtc::SdpVideoFormat& format,
      std::optional<std::string> scalability_mode) const override;
  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;
  std::unique_ptr<EncoderSelectorInterface> GetEncoderSelector() const override;

 private:
  std::unique_ptr<webrtc::VideoEncoderFactory> inner_;
  scoped_refptr<ExternalEncodedVideoFrameSenderImpl>* sender_ = nullptr;
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_ENCODED_VIDEO_FRAME_ENCODER_HXX
