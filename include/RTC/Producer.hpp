#ifndef MS_RTC_PRODUCER_HPP
#define MS_RTC_PRODUCER_HPP

#include "common.hpp"
#include "Channel/ChannelRequest.hpp"
#include "RTC/KeyFrameRequestManager.hpp"
#include "RTC/RTCP/CompoundPacket.hpp"
#include "RTC/RTCP/Packet.hpp"
#include "RTC/RTCP/SenderReport.hpp"
#include "RTC/RTCP/XrDelaySinceLastRr.hpp"
#include "RTC/RtpDictionaries.hpp"
#include "RTC/RtpHeaderExtensionIds.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/RtpStreamRecv.hpp"
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace RTC
{
	class Producer : public RTC::RtpStreamRecv::Listener, public RTC::KeyFrameRequestManager::Listener
	{
	public:
		class Listener
		{
		public:
			virtual ~Listener() = default;

		public:
			virtual void OnProducerPaused(RTC::Producer* producer)  = 0;
			virtual void OnProducerResumed(RTC::Producer* producer) = 0;
			virtual void OnProducerNewRtpStream(
			  RTC::Producer* producer, RTC::RtpStream* rtpStream, uint32_t mappedSsrc) = 0;
			virtual void OnProducerRtpStreamScore(
			  RTC::Producer* producer, RTC::RtpStream* rtpStream, uint8_t score, uint8_t previousScore) = 0;
			virtual void OnProducerRtcpSenderReport(
			  RTC::Producer* producer, RTC::RtpStream* rtpStream, bool first)                         = 0;
			virtual void OnProducerRtpPacketReceived(RTC::Producer* producer, RTC::RtpPacket* packet) = 0;
			virtual void OnProducerSendRtcpPacket(RTC::Producer* producer, RTC::RTCP::Packet* packet) = 0;
			virtual void OnProducerNeedWorstRemoteFractionLost(
			  RTC::Producer* producer, uint32_t mappedSsrc, uint8_t& worstRemoteFractionLost) = 0;
		};

	private:
		struct RtpEncodingMapping
		{
			std::string rid;
			uint32_t ssrc{ 0 };
			uint32_t mappedSsrc{ 0 }; // js中随机生成的一个种子
		};

	private:
		struct RtpMapping
		{
			std::map<uint8_t, uint8_t> codecs; // 编码的映射表 config.js 表对于的关系 [   client_codec_table_id ->server_codec_table_id ]
			std::vector<RtpEncodingMapping> encodings;
		};

	private:
		struct VideoOrientation
		{
			bool camera{ false };
			bool flip{ false };
			uint16_t rotation{ 0 };
		};

	public:
		enum class ReceiveRtpPacketResult
		{
			DISCARDED = 0,
			MEDIA     = 1,
			RETRANSMISSION
		};

	private:
		struct TraceEventTypes
		{
			bool rtp{ false };
			bool keyframe{ false };
			bool nack{ false };
			bool pli{ false };
			bool fir{ false };
		};

	public:
		Producer(const std::string& id, RTC::Producer::Listener* listener, json& data);
		virtual ~Producer();

	public:
		void FillJson(json& jsonObject) const;
		void FillJsonStats(json& jsonArray) const;
		void HandleRequest(Channel::ChannelRequest* request);
		RTC::Media::Kind GetKind() const
		{
			return this->kind;
		}
		const RTC::RtpParameters& GetRtpParameters() const
		{
			return this->rtpParameters;
		}
		const struct RTC::RtpHeaderExtensionIds& GetRtpHeaderExtensionIds() const
		{
			return this->rtpHeaderExtensionIds;
		}
		RTC::RtpParameters::Type GetType() const
		{
			return this->type;
		}
		bool IsPaused() const
		{
			return this->paused;
		}
		const std::map<RTC::RtpStreamRecv*, uint32_t>& GetRtpStreams()
		{
			return this->mapRtpStreamMappedSsrc;
		}
		const std::vector<uint8_t>* GetRtpStreamScores() const
		{
			return std::addressof(this->rtpStreamScores);
		}
		ReceiveRtpPacketResult ReceiveRtpPacket(RTC::RtpPacket* packet);
		void ReceiveRtcpSenderReport(RTC::RTCP::SenderReport* report);
		void ReceiveRtcpXrDelaySinceLastRr(RTC::RTCP::DelaySinceLastRr::SsrcInfo* ssrcInfo);
		void GetRtcp(RTC::RTCP::CompoundPacket* packet, uint64_t nowMs);
		void RequestKeyFrame(uint32_t mappedSsrc);

	private:
		RTC::RtpStreamRecv* GetRtpStream(RTC::RtpPacket* packet);
		RTC::RtpStreamRecv* CreateRtpStream(
		  RTC::RtpPacket* packet, const RTC::RtpCodecParameters& mediaCodec, size_t encodingIdx);
		void NotifyNewRtpStream(RTC::RtpStreamRecv* rtpStream);
		void PreProcessRtpPacket(RTC::RtpPacket* packet);
		bool MangleRtpPacket(RTC::RtpPacket* packet, RTC::RtpStreamRecv* rtpStream) const;
		void PostProcessRtpPacket(RTC::RtpPacket* packet);
		void EmitScore() const;
		void EmitTraceEventRtpAndKeyFrameTypes(RTC::RtpPacket* packet, bool isRtx = false) const;
		void EmitTraceEventKeyFrameType(RTC::RtpPacket* packet, bool isRtx = false) const;
		void EmitTraceEventPliType(uint32_t ssrc) const;
		void EmitTraceEventFirType(uint32_t ssrc) const;
		void EmitTraceEventNackType() const;

		/* Pure virtual methods inherited from RTC::RtpStreamRecv::Listener. */
	public:
		void OnRtpStreamScore(RTC::RtpStream* rtpStream, uint8_t score, uint8_t previousScore) override;
		void OnRtpStreamSendRtcpPacket(RTC::RtpStreamRecv* rtpStream, RTC::RTCP::Packet* packet) override;
		void OnRtpStreamNeedWorstRemoteFractionLost(
		  RTC::RtpStreamRecv* rtpStream, uint8_t& worstRemoteFractionLost) override;

		/* Pure virtual methods inherited from RTC::KeyFrameRequestManager::Listener. */
	public:
		void OnKeyFrameNeeded(RTC::KeyFrameRequestManager* keyFrameRequestManager, uint32_t ssrc) override;

	public:
		// Passed by argument.
		const std::string id;

	private:
		// Passed by argument.
		RTC::Producer::Listener* listener{ nullptr };
		// Allocated by this.
		std::map<uint32_t, RTC::RtpStreamRecv*> mapSsrcRtpStream;

		////////////////////////////////////////////////////////////////////////////////////////
		// TODO@chensong 请求关键帧的处理
		RTC::KeyFrameRequestManager* keyFrameRequestManager{ nullptr };
		// Others.
		RTC::Media::Kind kind; // 音频、视频、applicable 数据
		RTC::RtpParameters rtpParameters; // 编码类型
		RTC::RtpParameters::Type type{ RTC::RtpParameters::Type::NONE };  // 传输类型 webrtc、pip、svc、simulcat
		struct RtpMapping rtpMapping;// 选择编码对应映射表

		////////////////////////////////////////////////////////////////////////////////////////
		std::vector<RTC::RtpStreamRecv*> rtpStreamByEncodingIdx;

		// 推流ssrc的评分记录 时间复杂的O(1)
		std::vector<uint8_t> rtpStreamScores;

		// /////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//////////////   查找 根据producer_id <-->  consumer_id 的映射表
		std::map<uint32_t, RTC::RtpStreamRecv*> mapRtxSsrcRtpStream;
		std::map<RTC::RtpStreamRecv*, uint32_t> mapRtpStreamMappedSsrc;
		std::map<uint32_t, uint32_t> mapMappedSsrcSsrc;
		//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		/////////////////////////////////////////////////////////////////////////////////////////////
		//			WebRTC 中扩展协议
		struct RTC::RtpHeaderExtensionIds rtpHeaderExtensionIds;
		
		bool paused{ false };
		//////////////////////////////////////////////////////////////////////////////////////////////
		RTC::RtpPacket* currentRtpPacket{ nullptr };
		// Timestamp when last RTCP was sent.
		uint64_t lastRtcpSentTime{ 0u };
		uint16_t maxRtcpInterval{ 0u };
		// Video orientation.
		bool videoOrientationDetected{ false };
		struct VideoOrientation videoOrientation;
		struct TraceEventTypes traceEventTypes;
	};
} // namespace RTC

#endif
