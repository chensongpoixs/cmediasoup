/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 *
 *  作为整个码率预估及调整的核心
 */

#ifndef MODULES_CONGESTION_CONTROLLER_GOOG_CC_GOOG_CC_NETWORK_CONTROL_H_
#define MODULES_CONGESTION_CONTROLLER_GOOG_CC_GOOG_CC_NETWORK_CONTROL_H_

#include "api/network_state_predictor.h"
#include "api/transport/field_trial_based_config.h"
#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "api/transport/webrtc_key_value_config.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/timestamp.h"
#include "modules/bitrate_controller/send_side_bandwidth_estimation.h"
#include "modules/congestion_controller/goog_cc/acknowledged_bitrate_estimator.h"
#include "modules/congestion_controller/goog_cc/alr_detector.h"
#include "modules/congestion_controller/goog_cc/congestion_window_pushback_controller.h"
#include "modules/congestion_controller/goog_cc/delay_based_bwe.h"
#include "modules/congestion_controller/goog_cc/probe_controller.h"
#include "rtc_base/constructor_magic.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/experiments/rate_control_settings.h"

#include <absl/types/optional.h>
#include <stdint.h>
#include <deque>
#include <memory>
#include <vector>

namespace webrtc {
struct GoogCcConfig {
  std::unique_ptr<NetworkStateEstimator> network_state_estimator = nullptr;
  std::unique_ptr<NetworkStatePredictor> network_state_predictor = nullptr;
  bool feedback_only = false;
};

class GoogCcNetworkController : public NetworkControllerInterface {
 public:
  GoogCcNetworkController(NetworkControllerConfig config,
                          GoogCcConfig congestion_controller_config);
  ~GoogCcNetworkController() override;

  // NetworkControllerInterface
  NetworkControlUpdate OnNetworkAvailability(NetworkAvailability msg) override;
  NetworkControlUpdate OnNetworkRouteChange(NetworkRouteChange msg) override;
  NetworkControlUpdate OnProcessInterval(ProcessInterval msg) override;
  NetworkControlUpdate OnRemoteBitrateReport(RemoteBitrateReport msg) override;
  NetworkControlUpdate OnRoundTripTimeUpdate(RoundTripTimeUpdate msg) override;
  NetworkControlUpdate OnSentPacket(SentPacket msg) override;
  NetworkControlUpdate OnStreamsConfig(StreamsConfig msg) override;
  NetworkControlUpdate OnTargetRateConstraints(
      TargetRateConstraints msg) override;
  NetworkControlUpdate OnTransportLossReport(TransportLossReport msg) override;
  NetworkControlUpdate OnTransportPacketsFeedback(
      TransportPacketsFeedback msg) override;
  NetworkControlUpdate OnNetworkStateEstimate(
      NetworkStateEstimate msg) override;

  NetworkControlUpdate GetNetworkState(Timestamp at_time) const;

 private:
  friend class GoogCcStatePrinter;
  std::vector<ProbeClusterConfig> ResetConstraints(
      TargetRateConstraints new_constraints);
  void ClampConstraints();
  void MaybeTriggerOnNetworkChanged(NetworkControlUpdate* update,
                                    Timestamp at_time);
  void UpdateCongestionWindowSize(TimeDelta time_since_last_packet);
  PacerConfig GetPacingRates(Timestamp at_time) const;
  const FieldTrialBasedConfig trial_based_config_;

  const WebRtcKeyValueConfig* const key_value_config_;
  // RtcEventLog* const event_log_;
  const bool packet_feedback_only_;
  FieldTrialFlag safe_reset_on_route_change_;
  FieldTrialFlag safe_reset_acknowledged_rate_;
  const bool use_stable_bandwidth_estimate_;
  const bool use_downlink_delay_for_congestion_window_;
  const bool fall_back_to_probe_rate_;
  const bool use_min_allocatable_as_lower_bound_;
  const RateControlSettings rate_control_settings_;

	// 探测控制器，通过目标码率判断下次是否探测，探测码率大小
  const std::unique_ptr<ProbeController> probe_controller_;
	//基于当前的rtt设置一个时间窗口，同时基于当前的码率设置当前时间窗口下的数据量，
	// 通过判断当前窗口的使用量，如果使用量过大的时候，降低编码时使用的目标码率，加速窗口消退，减少延迟
  const std::unique_ptr<CongestionWindowPushbackController>  congestion_window_pushback_controller_;

	//  基于丢包计算预估码率，结合延迟预估码率，得到最终的目标码率
  std::unique_ptr<SendSideBandwidthEstimation> bandwidth_estimation_;
	// 应用(码率)受限检测，检测当前的发送码率是否和目标码率由于编码器等原因相差过大受限了，受限情况下会触发带宽预测过程的特殊处理
  std::unique_ptr<AlrDetector> alr_detector_;
	//根据feedback计算探测码率，PacingController中会将包按照cluster进行划分，
	// transport-CC报文能得到包所属的cluster以及发送和接收信息，
	// 通过发送和接收的数据大小比判断是否到达链路上限从而进行带宽探测
  std::unique_ptr<ProbeBitrateEstimator> probe_bitrate_estimator_;
	// NetworkStateEstimator 、 NetworkStatePredictor ： 此两者属于待开发类，只是在代码中有，但是还没开发完，没用上.
  std::unique_ptr<NetworkStateEstimator> network_estimator_;
  std::unique_ptr<NetworkStatePredictor> network_state_predictor_;
	// 基于延迟预估码率
	// ---> TrendlineEstimator : 使用线性回归计算当前网络拥堵情况
	// ---> AimdRateControl : 通过TrendLine预测出来的网络状态对码率进行aimd方式调整
  std::unique_ptr<DelayBasedBwe> delay_based_bwe_;
	// 估算当前的吞吐量 --> BitrateEstimator :使用滑动窗口 + 卡尔曼滤波计算当前发送吞吐量
  std::unique_ptr<AcknowledgedBitrateEstimator> acknowledged_bitrate_estimator_;

  absl::optional<NetworkControllerConfig> initial_config_;

  DataRate min_data_rate_ = DataRate::Zero();
  DataRate max_data_rate_ = DataRate::PlusInfinity();
  absl::optional<DataRate> starting_rate_;

  bool first_packet_sent_ = false;

  absl::optional<NetworkStateEstimate> estimate_;

  Timestamp next_loss_update_ = Timestamp::MinusInfinity();
  int lost_packets_since_last_loss_update_ = 0;
  int expected_packets_since_last_loss_update_ = 0;

  std::deque<int64_t> feedback_max_rtts_;

  DataRate last_raw_target_rate_;
  DataRate last_pushback_target_rate_;

  int32_t last_estimated_bitrate_bps_ = 0;
  uint8_t last_estimated_fraction_loss_ = 0;
  int64_t last_estimated_rtt_ms_ = 0;

  double pacing_factor_;
  DataRate min_total_allocated_bitrate_;
  DataRate max_padding_rate_;
  DataRate max_total_allocated_bitrate_;

  bool previously_in_alr_ = false;

  absl::optional<DataSize> current_data_window_;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(GoogCcNetworkController);
};

}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_GOOG_CC_GOOG_CC_NETWORK_CONTROL_H_
