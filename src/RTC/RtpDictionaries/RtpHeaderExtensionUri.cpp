#define MS_CLASS "RTC::RtpHeaderExtensionUri"
// #define MS_LOG_DEV_LEVEL 3

#include "Logger.hpp"
#include "Utils.hpp"
#include "RTC/RtpDictionaries.hpp"

namespace RTC
{
	/* Class variables. */

	// clang-format off
	std::unordered_map<std::string, RtpHeaderExtensionUri::Type> RtpHeaderExtensionUri::string2Type =
	{
		//  在 unified SDP 描述中 ‘a=mid’ 是每个 audio/video line 的必要元素，这个 header extension 将 SDP 中 ‘a=mid’ 后信息保存，
		// 用于标识一个 RTP packet 的 media 信息，可以作为一个 media 的唯一标识 
		{ "urn:ietf:params:rtp-hdrext:sdes:mid",                                       RtpHeaderExtensionUri::Type::MID                    },
		// 1. Media Source 等同于 WebRTC 中 Track 的概念，在 SDP 描述中可以使用 mid 作为唯一标识
		// 2. RTP Stream 是 RTP 流传输的最小流单位，例如在 Simulcast 或 SVC 场景中，一个 Media Source 中包含多个 RTP Stream，这时 SDP 中使用 ‘a=rid’ 来描述每个 RTP Stream
		{ "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id",                             RtpHeaderExtensionUri::Type::RTP_STREAM_ID          },
		// 用于声明重传时使用的 rid 标识	
		{ "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id",                    RtpHeaderExtensionUri::Type::REPAIRED_RTP_STREAM_ID },
		// 1. abs-send-time 为 一个 3 bytes 的时间数据
		// 2. REMB 计算需要 RTP 报文扩展头部 abs-send-time 的支持，用以记录 RTP 数据包在发送端的绝对发送时间
		{ "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time",                RtpHeaderExtensionUri::Type::ABS_SEND_TIME          },
//Transport-wide Congestion Control
		
		///////////////////////////////////////////////////////////////////////////////////////////////////
		// 有效数据只有 16 bit，记录了一个 sequence number 称为 transport-wide sequence number
		// 发送端在发送 RTP 数据包时，在 RTP 头部扩展中设置传输层序列号 TransportSequenceNumber；
		// 数据包到达接收端后记录该序列号和包到达时间，然后接收端基于此构造 TransportCC 报文返回到发送端；
		// 发送端解析该报文并执行 SendSide-BWE 算法，计算得到基于延迟的码率；
		// 最终和基于丢包率的码率进行比较得到最终目标码率
		{ "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01", RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01   },
		// NOTE: Remove this once framemarking draft becomes RFC.
		

		{ "http://tools.ietf.org/html/draft-ietf-avtext-framemarking-07",              RtpHeaderExtensionUri::Type::FRAME_MARKING_07       },
		
		
		// 由于 WebRTC 中 RTP payload 通过 SRTP 进行加密，这样导致 RTP packet 在经过交换节点或转发节点时，
		// 有些场景下需要知道当前 RTP packet 的编码信息，framemarking 用于给定该编码信息 
		{ "urn:ietf:params:rtp-hdrext:framemarking",                                   RtpHeaderExtensionUri::Type::FRAME_MARKING          },
		
		// 用于音量调节
		{ "urn:ietf:params:rtp-hdrext:ssrc-audio-level",                               RtpHeaderExtensionUri::Type::SSRC_AUDIO_LEVEL       },
		
		// 接收方收到以后，也会在响应中加入该 tag 属性，这样就完成了该功能的协商。Tag 中的 7 是扩展数据帧的扩展 id，
		// 可以是 1-15 中的任何一个 , 用来标识该方向数据的位置，具体参考 RFC5285。更多描述参考 RCC.07- 2.7.1.2.2。
		// 以上所述的视频方向其实包含了数据的方向和发送方 camera 的选项（前置或者后置），为了方便起见，一下都称为视频方向数据。
		{ "urn:3gpp:video-orientation",                                                RtpHeaderExtensionUri::Type::VIDEO_ORIENTATION      },
		
		// 传输时间偏移 (Transmission Time Offset)，offset 为 RTP packet 中 timestamp 与 实际发送时间的偏移
		{ "urn:ietf:params:rtp-hdrext:toffset",                                        RtpHeaderExtensionUri::Type::TOFFSET                }
	};
	// clang-format on

	/* Class methods. */

	RtpHeaderExtensionUri::Type RtpHeaderExtensionUri::GetType(std::string& uri)
	{
		MS_TRACE();

		// Force lowcase names.
		Utils::String::ToLowerCase(uri);

		auto it = RtpHeaderExtensionUri::string2Type.find(uri);

		if (it != RtpHeaderExtensionUri::string2Type.end())
			return it->second;

		return RtpHeaderExtensionUri::Type::UNKNOWN;
	}
} // namespace RTC
