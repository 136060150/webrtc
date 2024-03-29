/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/generic_decoder.h"

#include <vector>

#include "absl/types/optional.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "common_video/test/utilities.h"
#include "modules/video_coding/timing.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/event.h"
#include "system_wrappers/include/clock.h"
#include "test/fake_decoder.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace video_coding {

class ReceiveCallback : public VCMReceiveCallback {
 public:
  int32_t FrameToRender(VideoFrame& videoFrame,  // NOLINT
                        absl::optional<uint8_t> qp,
                        int32_t decode_time_ms,
                        VideoContentType content_type) override {
    {
      rtc::CritScope cs(&lock_);
      last_frame_ = videoFrame;
    }
    received_frame_event_.Set();
    return 0;
  }

  absl::optional<VideoFrame> GetLastFrame() {
    rtc::CritScope cs(&lock_);
    return last_frame_;
  }

  absl::optional<VideoFrame> WaitForFrame(int64_t wait_ms) {
    if (received_frame_event_.Wait(wait_ms)) {
      rtc::CritScope cs(&lock_);
      return last_frame_;
    } else {
      return absl::nullopt;
    }
  }

 private:
  rtc::CriticalSection lock_;
  rtc::Event received_frame_event_;
  absl::optional<VideoFrame> last_frame_ RTC_GUARDED_BY(lock_);
};

class GenericDecoderTest : public ::testing::Test {
 protected:
  GenericDecoderTest()
      : clock_(0),
        timing_(&clock_),
        task_queue_factory_(CreateDefaultTaskQueueFactory()),
        decoder_(task_queue_factory_.get()),
        vcm_callback_(&timing_, &clock_),
        generic_decoder_(&decoder_, /*isExternal=*/true) {}

  void SetUp() override {
    generic_decoder_.RegisterDecodeCompleteCallback(&vcm_callback_);
    vcm_callback_.SetUserReceiveCallback(&user_callback_);
    VideoCodec settings;
    settings.codecType = kVideoCodecVP8;
    settings.width = 10;
    settings.height = 10;
    generic_decoder_.InitDecode(&settings, /*numberOfCores=*/4);
  }

  SimulatedClock clock_;
  VCMTiming timing_;
  std::unique_ptr<TaskQueueFactory> task_queue_factory_;
  webrtc::test::FakeDecoder decoder_;
  VCMDecodedFrameCallback vcm_callback_;
  VCMGenericDecoder generic_decoder_;
  ReceiveCallback user_callback_;
};

TEST_F(GenericDecoderTest, PassesColorSpace) {
  webrtc::ColorSpace color_space =
      CreateTestColorSpace(/*with_hdr_metadata=*/true);
  VCMEncodedFrame encoded_frame;
  encoded_frame.SetColorSpace(color_space);
  generic_decoder_.Decode(encoded_frame, clock_.TimeInMilliseconds());
  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(10);
  ASSERT_TRUE(decoded_frame.has_value());
  absl::optional<webrtc::ColorSpace> decoded_color_space =
      decoded_frame->color_space();
  ASSERT_TRUE(decoded_color_space.has_value());
  EXPECT_EQ(*decoded_color_space, color_space);
}

TEST_F(GenericDecoderTest, PassesColorSpaceForDelayedDecoders) {
  webrtc::ColorSpace color_space =
      CreateTestColorSpace(/*with_hdr_metadata=*/true);
  decoder_.SetDelayedDecoding(100);

  {
    // Ensure the original frame is destroyed before the decoding is completed.
    VCMEncodedFrame encoded_frame;
    encoded_frame.SetColorSpace(color_space);
    generic_decoder_.Decode(encoded_frame, clock_.TimeInMilliseconds());
  }

  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(200);
  ASSERT_TRUE(decoded_frame.has_value());
  absl::optional<webrtc::ColorSpace> decoded_color_space =
      decoded_frame->color_space();
  ASSERT_TRUE(decoded_color_space.has_value());
  EXPECT_EQ(*decoded_color_space, color_space);
}

TEST_F(GenericDecoderTest, PassesPacketInfos) {
  RtpPacketInfos packet_infos = CreatePacketInfos(3);
  VCMEncodedFrame encoded_frame;
  encoded_frame.SetPacketInfos(packet_infos);
  generic_decoder_.Decode(encoded_frame, clock_.TimeInMilliseconds());
  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(10);
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_EQ(decoded_frame->packet_infos().size(), 3U);
}

TEST_F(GenericDecoderTest, PassesPacketInfosForDelayedDecoders) {
  RtpPacketInfos packet_infos = CreatePacketInfos(3);
  decoder_.SetDelayedDecoding(100);

  {
    // Ensure the original frame is destroyed before the decoding is completed.
    VCMEncodedFrame encoded_frame;
    encoded_frame.SetPacketInfos(packet_infos);
    generic_decoder_.Decode(encoded_frame, clock_.TimeInMilliseconds());
  }

  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(200);
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_EQ(decoded_frame->packet_infos().size(), 3U);
}

}  // namespace video_coding
}  // namespace webrtc
