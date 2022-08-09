/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 * 基于当前的rtt设置一个时间窗口，同时基于当前的码率设置当前时间窗口下的数据量，通过判断当前窗口的使用量，如果使用量过大的时候，降低编码时使用的目标码率，加速窗口消退，减少延迟
 */

#ifndef MODULES_CONGESTION_CONTROLLER_GOOG_CC_CONGESTION_WINDOW_PUSHBACK_CONTROLLER_H_
#define MODULES_CONGESTION_CONTROLLER_GOOG_CC_CONGESTION_WINDOW_PUSHBACK_CONTROLLER_H_

#include "api/transport/webrtc_key_value_config.h"
#include "api/units/data_size.h"

#include <absl/types/optional.h>
#include <stddef.h>
#include <stdint.h>

namespace webrtc {

// This class enables pushback from congestion window directly to video encoder.
// When the congestion window is filling up, the video encoder target bitrate
// will be reduced accordingly to accommodate the network changes. To avoid
// pausing video too frequently, a minimum encoder target bitrate threshold is
// used to prevent video pause due to a full congestion window.
class CongestionWindowPushbackController {
 public:
  explicit CongestionWindowPushbackController(
      const WebRtcKeyValueConfig* key_value_config);
  CongestionWindowPushbackController(
      const WebRtcKeyValueConfig* key_value_config,
      uint32_t min_pushback_target_bitrate_bps);
  void UpdateOutstandingData(int64_t outstanding_bytes);
  void UpdatePacingQueue(int64_t pacing_bytes);
  void UpdateMaxOutstandingData(size_t max_outstanding_bytes);
  uint32_t UpdateTargetBitrate(uint32_t bitrate_bps);
  void SetDataWindow(DataSize data_window);

 private:
  absl::optional<DataSize> current_data_window_;
  int64_t outstanding_bytes_ = 0;
  int64_t pacing_bytes_ = 0;
  const bool add_pacing_;
  const uint32_t min_pushback_target_bitrate_bps_;
  double encoding_rate_ratio_ = 1.0;
};

}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_GOOG_CC_CONGESTION_WINDOW_PUSHBACK_CONTROLLER_H_
