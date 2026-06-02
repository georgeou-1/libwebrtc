#ifndef LIB_WEBRTC_RTC_ENCODED_VIDEO_FRAME_DECODER_HXX
#define LIB_WEBRTC_RTC_ENCODED_VIDEO_FRAME_DECODER_HXX

#include <atomic>
#include <memory>
#include <vector>

#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "rtc_encoded_video_frame.h"

namespace libwebrtc {

class EncodedVideoFrameForwardingDecoderFactory final
    : public webrtc::VideoDecoderFactory {
 public:
  explicit EncodedVideoFrameForwardingDecoderFactory(
      std::unique_ptr<webrtc::VideoDecoderFactory> inner,
      RTCEncodedVideoFrameReceiver** receiver);

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(
      const webrtc::SdpVideoFormat& format) override;

 private:
  std::unique_ptr<webrtc::VideoDecoderFactory> inner_;
  RTCEncodedVideoFrameReceiver** receiver_ = nullptr;
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_ENCODED_VIDEO_FRAME_DECODER_HXX
