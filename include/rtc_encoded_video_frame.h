#ifndef LIB_WEBRTC_RTC_ENCODED_VIDEO_FRAME_HXX
#define LIB_WEBRTC_RTC_ENCODED_VIDEO_FRAME_HXX

#include <cstddef>
#include <cstdint>

#include "rtc_types.h"

namespace libwebrtc {

enum class RTCEncodedVideoCodec {
  kUnknown = 0,
  kAV1 = 1,
  kVP8 = 2,
  kVP9 = 3,
  kH264 = 4,
  kH265 = 5,
};

struct RTCEncodedVideoFrame {
  RTCEncodedVideoCodec codec = RTCEncodedVideoCodec::kUnknown;
  const uint8_t* data = nullptr;
  size_t size = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t rtp_timestamp = 0;
  int64_t duration_90k = 3000;
  int64_t capture_time_ms = 0;
  int64_t ntp_time_ms = 0;
  uint64_t frame_sequence = 0;
  uint64_t tracking_frame_id = 0;
  bool key_frame = false;
};

class RTCEncodedVideoFrameReceiver {
 public:
  virtual ~RTCEncodedVideoFrameReceiver() {}

  virtual void OnEncodedVideoFrame(const RTCEncodedVideoFrame& frame) = 0;
};

class RTCEncodedVideoFrameSender : public RefCountInterface {
 public:
  virtual bool SubmitEncodedVideoFrame(
      const RTCEncodedVideoFrame& frame) = 0;

  virtual bool WantsKeyFrame() const = 0;

 protected:
  virtual ~RTCEncodedVideoFrameSender() {}
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_ENCODED_VIDEO_FRAME_HXX
