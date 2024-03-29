﻿#define MS_CLASS "RTC::Producer"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/Producer.hpp"
#include "DepLibUV.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"
#include "Utils.hpp"
#include "Channel/ChannelNotifier.hpp"
#include "RTC/Codecs/Tools.hpp"
#include "RTC/RTCP/FeedbackPs.hpp"
#include "RTC/RTCP/FeedbackRtp.hpp"
#include "RTC/RTCP/XrReceiverReferenceTime.hpp"
#include <cstring>  // std::memcpy()
#include <iterator> // std::ostream_iterator
#include <sstream>  // std::ostringstream

namespace RTC
{
	/* Instance methods. */

	Producer::Producer(const std::string& id, RTC::Producer::Listener* listener, json& data)
	  : id(id), listener(listener)
	{
		MS_TRACE();
		/*
		{
					"kind":"video",
					"paused":false,
					"rtpMapping":{
						"codecs":[
							{
								"mappedPayloadType":101,
								"payloadType":108
							},
							{
								"mappedPayloadType":102,
								"payloadType":109
							}
						],
						"encodings":[
							{
								"mappedSsrc":806764358,
								"rid":"r0",
								"scalabilityMode":"S1T3"
							},
							{
								"mappedSsrc":806764359,
								"rid":"r1",
								"scalabilityMode":"S1T3"
							},
							{
								"mappedSsrc":806764360,
								"rid":"r2",
								"scalabilityMode":"S1T3"
							}
						]
					},
					"rtpParameters":{
						"codecs":[
						{
						"clockRate":90000,
						"mimeType":"video/H264",
						"parameters":{
						"level-asymmetry-allowed":1,
						"packetization-mode":1,
						"profile-level-id":"42e01f"
					},
						"payloadType":108,
					"rtcpFeedback":[
					{
						"parameter":"",
						"type":"goog-remb"
					},
					{
						"parameter":"",
						"type":"transport-cc"
					},
					{
						"parameter":"fir",
						"type":"ccm"
					},
					{
						"parameter":"",
						"type":"nack"
					},
					{
						"parameter":"pli",
						"type":"nack"
					}
					]
					},
					{
						"clockRate":90000,
						"mimeType":"video/rtx",
						"parameters":{
						"apt":108
						},
						"payloadType":109,
						"rtcpFeedback":[

					]
					}
					],
					"encodings":[
					{
						"active":true,
						"dtx":false,
						"maxBitrate":500000,
						"rid":"r0",
						"scalabilityMode":"S1T3",
						"scaleResolutionDownBy":4
					},
					{
						"active":true,
						"dtx":false,
						"maxBitrate":1000000,
						"rid":"r1",
						"scalabilityMode":"S1T3",
						"scaleResolutionDownBy":2
					},
					{
						"active":true,
						"dtx":false,
						"maxBitrate":5000000,
						"rid":"r2",
						"scalabilityMode":"S1T3",
						"scaleResolutionDownBy":1
					}
					],
					"headerExtensions":[
					{
						"encrypt":false,
						"id":4,
						"parameters":{

						},
						"uri":"urn:ietf:params:rtp-hdrext:sdes:mid"
					},
					{
						"encrypt":false,
						"id":10,
						"parameters":{

						},
						"uri":"urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id"
					},
					{
						"encrypt":false,
						"id":11,
						"parameters":{

						},
						"uri":"urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id"
					},
					{
						"encrypt":false,
						"id":2,
						"parameters":{

						},
					"uri":"http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"
					},
					{
						"encrypt":false,
						"id":3,
						"parameters":{

						},
					"uri":"http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"
					},
					{
						"encrypt":false,
						"id":13,
						"parameters":{

						},
						"uri":"urn:3gpp:video-orientation"
					},
					{
						"encrypt":false,
						"id":14,
						"parameters":{

						},
					"uri":"urn:ietf:params:rtp-hdrext:toffset"
					}
						],
						"mid":"2",
						"rtcp":{
						"cname":"UgYi3789TL6C/8Zx",
						"reducedSize":true
						}
					}
				}
		
		*/
		auto jsonKindIt = data.find("kind");

		if (jsonKindIt == data.end() || !jsonKindIt->is_string())
		{
			MS_THROW_TYPE_ERROR("missing kind");
		}

		// This may throw.
		this->kind = RTC::Media::GetKind(jsonKindIt->get<std::string>());

		if (this->kind == RTC::Media::Kind::ALL)
		{
			MS_THROW_TYPE_ERROR("invalid empty kind");
		}

		auto jsonRtpParametersIt = data.find("rtpParameters");

		if (jsonRtpParametersIt == data.end() || !jsonRtpParametersIt->is_object())
		{
			MS_THROW_TYPE_ERROR("missing rtpParameters");
		}

		// This may throw.
		this->rtpParameters = RTC::RtpParameters(*jsonRtpParametersIt);

		// Evaluate type.
		// 根据rtpParameters中encodings中设置编码传输的类型
		this->type = RTC::RtpParameters::GetType(this->rtpParameters);

		// Reserve a slot in rtpStreamByEncodingIdx and rtpStreamsScores vectors
		// for each RTP stream.
		// 根据客户端 的编码类型 设置每一种编码类型
		this->rtpStreamByEncodingIdx.resize(this->rtpParameters.encodings.size(), nullptr);
		this->rtpStreamScores.resize(this->rtpParameters.encodings.size(), 0u);

		auto& encoding   = this->rtpParameters.encodings[0];
		auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);

		// 检查对编码类型判断传输类型是否准确创建
		if (!RTC::Codecs::Tools::IsValidTypeForCodec(this->type, mediaCodec->mimeType))
		{
			MS_THROW_TYPE_ERROR(
			  "%s codec not supported for %s",
			  mediaCodec->mimeType.ToString().c_str(),
			  RTC::RtpParameters::GetTypeString(this->type).c_str());
		}

		////////////////////////////////////////////////////////////////////////////////////////
		// const DynamicPayloadTypes = [
		//		    100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
		//		    111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
		//		    122, 123, 124, 125, 126, 127, 96, 97, 98, 99
		//		];
		//  
		//	    [  client_codec_table_id -> server_codec_table_id]
		// 
		//		"mappedPayloadType":101,===>  config.js 编码类型 经过ortc.js 方法[generateRouterRtpCapabilities 中DynamicPayloadTypes数组]
		// 都编码排序 的playloedType的类型 方法
		//							"payloadType":108 // 对于客户端payloadType的类型 
		//						 
		////////////////////////////////////////////////////////////////////////////////////////
		auto jsonRtpMappingIt = data.find("rtpMapping");

		if (jsonRtpMappingIt == data.end() || !jsonRtpMappingIt->is_object())
		{
			MS_THROW_TYPE_ERROR("missing rtpMapping");
		}

		auto jsonCodecsIt = jsonRtpMappingIt->find("codecs");

		if (jsonCodecsIt == jsonRtpMappingIt->end() || !jsonCodecsIt->is_array())
		{
			MS_THROW_TYPE_ERROR("missing rtpMapping.codecs");
		}

		for (auto& codec : *jsonCodecsIt)
		{
			if (!codec.is_object())
			{
				MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.codecs (not an object)");
			}

			auto jsonPayloadTypeIt = codec.find("payloadType");

			// clang-format off
			if (
				jsonPayloadTypeIt == codec.end() ||
				!Utils::Json::IsPositiveInteger(*jsonPayloadTypeIt)
			)
			// clang-format on
			{
				MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.codecs (missing payloadType)");
			}

			auto jsonMappedPayloadTypeIt = codec.find("mappedPayloadType");

			// clang-format off
			if (
				jsonMappedPayloadTypeIt == codec.end() ||
				!Utils::Json::IsPositiveInteger(*jsonMappedPayloadTypeIt)
			)
			// clang-format on
			{
				MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.codecs (missing mappedPayloadType)");
			}

			this->rtpMapping.codecs[jsonPayloadTypeIt->get<uint8_t>()] =
			  jsonMappedPayloadTypeIt->get<uint8_t>();
		}

		////////////////////////////////////////////////////////////////////////////////////////////////////
		//
		//    对于js中代码 ortc.js 中getProducerRtpParametersMapping方法
		//    // Generate encodings mapping.
		//    // 就是一个随机种子 ssrc 
		//    let mappedSsrc = utils.generateRandomNumber();
		//    for (const encoding of params.encodings) {
		//        const mappedEncoding = {};
		//        mappedEncoding.mappedSsrc = mappedSsrc++;
		//        if (encoding.rid)
		//            mappedEncoding.rid = encoding.rid;
		//        if (encoding.ssrc)
		//            mappedEncoding.ssrc = encoding.ssrc;
		//        if (encoding.scalabilityMode)
		//            mappedEncoding.scalabilityMode = encoding.scalabilityMode;
		//        rtpMapping.encodings.push(mappedEncoding);
		//    }
		//
		//
		//
		////////////////////////////////////////////////////////////////////////////////////////////////////

		auto jsonEncodingsIt = jsonRtpMappingIt->find("encodings");

		if (jsonEncodingsIt == jsonRtpMappingIt->end() || !jsonEncodingsIt->is_array())
		{
			MS_THROW_TYPE_ERROR("missing rtpMapping.encodings");
		}

		this->rtpMapping.encodings.reserve(jsonEncodingsIt->size());

		for (auto& encoding : *jsonEncodingsIt)
		{
			if (!encoding.is_object())
			{
				MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.encodings");
			}

			this->rtpMapping.encodings.emplace_back();

			auto& encodingMapping = this->rtpMapping.encodings.back();

			// ssrc is optional.
			auto jsonSsrcIt = encoding.find("ssrc");

			// clang-format off
			if (
				jsonSsrcIt != encoding.end() &&
				Utils::Json::IsPositiveInteger(*jsonSsrcIt)
			)
			// clang-format on
			{
				encodingMapping.ssrc = jsonSsrcIt->get<uint32_t>();
			}

			// rid is optional.
			auto jsonRidIt = encoding.find("rid");

			if (jsonRidIt != encoding.end() && jsonRidIt->is_string())
			{
				encodingMapping.rid = jsonRidIt->get<std::string>();
			}

			// However ssrc or rid must be present (if more than 1 encoding).
			// clang-format off
			if (
				jsonEncodingsIt->size() > 1 &&
				jsonSsrcIt == encoding.end() &&
				jsonRidIt == encoding.end()
			)
			// clang-format on
			{
				MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.encodings (missing ssrc or rid)");
			}

			// If there is no mid and a single encoding, ssrc or rid must be present.
			// clang-format off
			if (
				this->rtpParameters.mid.empty() &&
				jsonEncodingsIt->size() == 1 &&
				jsonSsrcIt == encoding.end() &&
				jsonRidIt == encoding.end()
			)
			// clang-format on
			{
				MS_THROW_TYPE_ERROR(
				  "wrong entry in rtpMapping.encodings (missing ssrc or rid, or rtpParameters.mid)");
			}

			// mappedSsrc is mandatory.
			auto jsonMappedSsrcIt = encoding.find("mappedSsrc");

			// clang-format off
			if (
				jsonMappedSsrcIt == encoding.end() ||
				!Utils::Json::IsPositiveInteger(*jsonMappedSsrcIt)
			)
			// clang-format on
			{
				MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.encodings (missing mappedSsrc)");
			}

			encodingMapping.mappedSsrc = jsonMappedSsrcIt->get<uint32_t>();
		}

		auto jsonPausedIt = data.find("paused");

		if (jsonPausedIt != data.end() && jsonPausedIt->is_boolean())
		{
			this->paused = jsonPausedIt->get<bool>();
		}

		// The number of encodings in rtpParameters must match the number of encodings
		// in rtpMapping.
		if (this->rtpParameters.encodings.size() != this->rtpMapping.encodings.size())
		{
			MS_THROW_TYPE_ERROR("rtpParameters.encodings size does not match rtpMapping.encodings size");
		}

		// Fill RTP header extension ids.
		// WebRTC 中扩展协议 ids
		// This may throw.
		for (auto& exten : this->rtpParameters.headerExtensions)
		{
			if (exten.id == 0u)
			{
				MS_THROW_TYPE_ERROR("RTP extension id cannot be 0");
			}
			//  在 unified SDP 描述中 ‘a=mid’ 是每个 audio/video line 的必要元素，这个 header extension 将 SDP 中 ‘a=mid’ 后信息保存，
			// 用于标识一个 RTP packet 的 media 信息，可以作为一个 media 的唯一标识 
			if (this->rtpHeaderExtensionIds.mid == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::MID)
			{
				this->rtpHeaderExtensionIds.mid = exten.id;
			}

			// 1. Media Source 等同于 WebRTC 中 Track 的概念，在 SDP 描述中可以使用 mid 作为唯一标识
			// 2. RTP Stream 是 RTP 流传输的最小流单位，例如在 Simulcast 或 SVC 场景中，一个 Media Source 中包含多个 RTP Stream，这时 SDP 中使用 ‘a=rid’ 来描述每个 RTP Stream
			if (this->rtpHeaderExtensionIds.rid == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::RTP_STREAM_ID)
			{
				this->rtpHeaderExtensionIds.rid = exten.id;
			}
			// 用于声明重传时使用的 rid 标识	
			if (this->rtpHeaderExtensionIds.rrid == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::REPAIRED_RTP_STREAM_ID)
			{
				this->rtpHeaderExtensionIds.rrid = exten.id;
			}
			// 1. abs-send-time 为 一个 3 bytes 的时间数据
			// 2. REMB 计算需要 RTP 报文扩展头部 abs-send-time 的支持，用以记录 RTP 数据包在发送端的绝对发送时间
			if (this->rtpHeaderExtensionIds.absSendTime == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::ABS_SEND_TIME)
			{
				this->rtpHeaderExtensionIds.absSendTime = exten.id;
			}

			// 有效数据只有 16 bit，记录了一个 sequence number 称为 transport-wide sequence number
			// 发送端在发送 RTP 数据包时，在 RTP 头部扩展中设置传输层序列号 TransportSequenceNumber；
			// 数据包到达接收端后记录该序列号和包到达时间，然后接收端基于此构造 TransportCC 报文返回到发送端；
			// 发送端解析该报文并执行 SendSide-BWE 算法，计算得到基于延迟的码率；
			// 最终和基于丢包率的码率进行比较得到最终目标码率
			if (this->rtpHeaderExtensionIds.transportWideCc01 == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01)
			{
				this->rtpHeaderExtensionIds.transportWideCc01 = exten.id;
			}

			// NOTE: Remove this once framemarking draft becomes RFC.
			if (this->rtpHeaderExtensionIds.frameMarking07 == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING_07)
			{
				this->rtpHeaderExtensionIds.frameMarking07 = exten.id;
			}
			// 由于 WebRTC 中 RTP payload 通过 SRTP 进行加密，这样导致 RTP packet 在经过交换节点或转发节点时，
			// 有些场景下需要知道当前 RTP packet 的编码信息，framemarking 用于给定该编码信息 
			if (this->rtpHeaderExtensionIds.frameMarking == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING)
			{
				this->rtpHeaderExtensionIds.frameMarking = exten.id;
			}
			// 用于音量调节
			if (this->rtpHeaderExtensionIds.ssrcAudioLevel == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::SSRC_AUDIO_LEVEL)
			{
				this->rtpHeaderExtensionIds.ssrcAudioLevel = exten.id;
			}

			// 接收方收到以后，也会在响应中加入该 tag 属性，这样就完成了该功能的协商。Tag 中的 7 是扩展数据帧的扩展 id，
			// 可以是 1-15 中的任何一个 , 用来标识该方向数据的位置，具体参考 RFC5285。更多描述参考 RCC.07- 2.7.1.2.2。
			// 以上所述的视频方向其实包含了数据的方向和发送方 camera 的选项（前置或者后置），为了方便起见，一下都称为视频方向数据。
			if (this->rtpHeaderExtensionIds.videoOrientation == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::VIDEO_ORIENTATION)
			{
				this->rtpHeaderExtensionIds.videoOrientation = exten.id;
			}
			// 传输时间偏移 (Transmission Time Offset)，offset 为 RTP packet 中 timestamp 与 实际发送时间的偏移
			if (this->rtpHeaderExtensionIds.toffset == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::TOFFSET)
			{
				this->rtpHeaderExtensionIds.toffset = exten.id;
			}
		}

		// Set the RTCP report generation interval.
		if (this->kind == RTC::Media::Kind::AUDIO)
		{
			this->maxRtcpInterval = RTC::RTCP::MaxAudioIntervalMs;
		}
		else
		{
			this->maxRtcpInterval = RTC::RTCP::MaxVideoIntervalMs;
		}

		// Create a KeyFrameRequestManager.
		if (this->kind == RTC::Media::Kind::VIDEO)
		{
			auto jsonKeyFrameRequestDelayIt = data.find("keyFrameRequestDelay");
			uint32_t keyFrameRequestDelay   = 0u;

			// clang-format off
			if (
				jsonKeyFrameRequestDelayIt != data.end() &&
				jsonKeyFrameRequestDelayIt->is_number_integer()
			)
			// clang-format on
			{
				keyFrameRequestDelay = jsonKeyFrameRequestDelayIt->get<uint32_t>();
			}

			this->keyFrameRequestManager = new RTC::KeyFrameRequestManager(this, keyFrameRequestDelay);
		}
	}

	Producer::~Producer()
	{
		MS_TRACE();

		// Delete all streams.
		for (auto& kv : this->mapSsrcRtpStream)
		{
			auto* rtpStream = kv.second;

			delete rtpStream;
		}

		this->mapSsrcRtpStream.clear();
		this->rtpStreamByEncodingIdx.clear();
		this->rtpStreamScores.clear();
		this->mapRtxSsrcRtpStream.clear();
		this->mapRtpStreamMappedSsrc.clear();
		this->mapMappedSsrcSsrc.clear();

		// Delete the KeyFrameRequestManager.
		delete this->keyFrameRequestManager;
	}

	void Producer::FillJson(json& jsonObject) const
	{
		MS_TRACE();

		// Add id.
		jsonObject["id"] = this->id;

		// Add kind.
		jsonObject["kind"] = RTC::Media::GetString(this->kind);

		// Add rtpParameters.
		this->rtpParameters.FillJson(jsonObject["rtpParameters"]);

		// Add type.
		jsonObject["type"] = RTC::RtpParameters::GetTypeString(this->type);

		// Add rtpMapping.
		jsonObject["rtpMapping"] = json::object();
		auto jsonRtpMappingIt    = jsonObject.find("rtpMapping");

		// Add rtpMapping.codecs.
		{
			(*jsonRtpMappingIt)["codecs"] = json::array();
			auto jsonCodecsIt             = jsonRtpMappingIt->find("codecs");
			size_t idx{ 0 };

			for (auto& kv : this->rtpMapping.codecs)
			{
				jsonCodecsIt->emplace_back(json::value_t::object);

				auto& jsonEntry        = (*jsonCodecsIt)[idx];
				auto payloadType       = kv.first;
				auto mappedPayloadType = kv.second;

				jsonEntry["payloadType"]       = payloadType;
				jsonEntry["mappedPayloadType"] = mappedPayloadType;

				++idx;
			}
		}

		// Add rtpMapping.encodings.
		{
			(*jsonRtpMappingIt)["encodings"] = json::array();
			auto jsonEncodingsIt             = jsonRtpMappingIt->find("encodings");

			for (size_t i{ 0 }; i < this->rtpMapping.encodings.size(); ++i)
			{
				jsonEncodingsIt->emplace_back(json::value_t::object);

				auto& jsonEntry             = (*jsonEncodingsIt)[i];
				const auto& encodingMapping = this->rtpMapping.encodings[i];

				if (!encodingMapping.rid.empty())
					jsonEntry["rid"] = encodingMapping.rid;
				else
					jsonEntry["rid"] = nullptr;

				if (encodingMapping.ssrc != 0u)
					jsonEntry["ssrc"] = encodingMapping.ssrc;
				else
					jsonEntry["ssrc"] = nullptr;

				jsonEntry["mappedSsrc"] = encodingMapping.mappedSsrc;
			}
		}

		// Add rtpStreams.
		jsonObject["rtpStreams"] = json::array();
		auto jsonRtpStreamsIt    = jsonObject.find("rtpStreams");

		for (auto* rtpStream : this->rtpStreamByEncodingIdx)
		{
			if (!rtpStream)
				continue;

			jsonRtpStreamsIt->emplace_back(json::value_t::object);

			auto& jsonEntry = (*jsonRtpStreamsIt)[jsonRtpStreamsIt->size() - 1];

			rtpStream->FillJson(jsonEntry);
		}

		// Add paused.
		jsonObject["paused"] = this->paused;

		// Add traceEventTypes.
		std::vector<std::string> traceEventTypes;
		std::ostringstream traceEventTypesStream;

		if (this->traceEventTypes.rtp)
			traceEventTypes.emplace_back("rtp");
		if (this->traceEventTypes.keyframe)
			traceEventTypes.emplace_back("keyframe");
		if (this->traceEventTypes.nack)
			traceEventTypes.emplace_back("nack");
		if (this->traceEventTypes.pli)
			traceEventTypes.emplace_back("pli");
		if (this->traceEventTypes.fir)
			traceEventTypes.emplace_back("fir");

		if (!traceEventTypes.empty())
		{
			std::copy(
			  traceEventTypes.begin(),
			  traceEventTypes.end() - 1,
			  std::ostream_iterator<std::string>(traceEventTypesStream, ","));
			traceEventTypesStream << traceEventTypes.back();
		}

		jsonObject["traceEventTypes"] = traceEventTypesStream.str();
	}

	void Producer::FillJsonStats(json& jsonArray) const
	{
		MS_TRACE();

		for (auto* rtpStream : this->rtpStreamByEncodingIdx)
		{
			if (!rtpStream)
				continue;

			jsonArray.emplace_back(json::value_t::object);

			auto& jsonEntry = jsonArray[jsonArray.size() - 1];

			rtpStream->FillJsonStats(jsonEntry);
		}
	}

	void Producer::HandleRequest(Channel::ChannelRequest* request)
	{
		MS_TRACE();

		switch (request->methodId)
		{
			case Channel::ChannelRequest::MethodId::PRODUCER_DUMP:
			{
				json data = json::object();

				FillJson(data);

				request->Accept(data);

				break;
			}

			case Channel::ChannelRequest::MethodId::PRODUCER_GET_STATS:
			{
				json data = json::array();

				FillJsonStats(data);

				request->Accept(data);

				break;
			}

			case Channel::ChannelRequest::MethodId::PRODUCER_PAUSE:
			{
				if (this->paused)
				{
					request->Accept();

					return;
				}

				// Pause all streams.
				for (auto& kv : this->mapSsrcRtpStream)
				{
					auto* rtpStream = kv.second;

					rtpStream->Pause();
				}

				this->paused = true;

				MS_DEBUG_DEV("Producer paused [producerId:%s]", this->id.c_str());

				this->listener->OnProducerPaused(this);

				request->Accept();

				break;
			}

			case Channel::ChannelRequest::MethodId::PRODUCER_RESUME:
			{
				if (!this->paused)
				{
					request->Accept();

					return;
				}

				// Resume all streams.
				for (auto& kv : this->mapSsrcRtpStream)
				{
					auto* rtpStream = kv.second;

					rtpStream->Resume();
				}

				this->paused = false;

				MS_DEBUG_DEV("Producer resumed [producerId:%s]", this->id.c_str());

				this->listener->OnProducerResumed(this);

				if (this->keyFrameRequestManager)
				{
					MS_DEBUG_2TAGS(rtcp, rtx, "requesting forced key frame(s) after resumed");

					// Request a key frame for all streams.
					for (auto& kv : this->mapSsrcRtpStream)
					{
						auto ssrc = kv.first;

						this->keyFrameRequestManager->ForceKeyFrameNeeded(ssrc);
					}
				}

				request->Accept();

				break;
			}

			case Channel::ChannelRequest::MethodId::PRODUCER_ENABLE_TRACE_EVENT:
			{
				auto jsonTypesIt = request->data.find("types");

				// Disable all if no entries.
				if (jsonTypesIt == request->data.end() || !jsonTypesIt->is_array())
					MS_THROW_TYPE_ERROR("wrong types (not an array)");

				// Reset traceEventTypes.
				struct TraceEventTypes newTraceEventTypes;

				for (const auto& type : *jsonTypesIt)
				{
					if (!type.is_string())
						MS_THROW_TYPE_ERROR("wrong type (not a string)");

					std::string typeStr = type.get<std::string>();

					if (typeStr == "rtp")
						newTraceEventTypes.rtp = true;
					else if (typeStr == "keyframe")
						newTraceEventTypes.keyframe = true;
					else if (typeStr == "nack")
						newTraceEventTypes.nack = true;
					else if (typeStr == "pli")
						newTraceEventTypes.pli = true;
					else if (typeStr == "fir")
						newTraceEventTypes.fir = true;
				}

				this->traceEventTypes = newTraceEventTypes;

				request->Accept();

				break;
			}

			default:
			{
				MS_THROW_ERROR("unknown method '%s'", request->method.c_str());
			}
		}
	}

	Producer::ReceiveRtpPacketResult Producer::ReceiveRtpPacket(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		// Reset current packet.
		this->currentRtpPacket = nullptr;

		// Count number of RTP streams.
		auto numRtpStreamsBefore = this->mapSsrcRtpStream.size();

		auto* rtpStream = GetRtpStream(packet);

		if (!rtpStream)
		{
			MS_WARN_TAG(rtp, "no stream found for received packet [ssrc:%" PRIu32 "]", packet->GetSsrc());

			return ReceiveRtpPacketResult::DISCARDED;
		}

		// Pre-process the packet.
		PreProcessRtpPacket(packet);

		ReceiveRtpPacketResult result;
		bool isRtx{ false };

		// Media packet.
		if (packet->GetSsrc() == rtpStream->GetSsrc())
		{
			result = ReceiveRtpPacketResult::MEDIA;

			// Process the packet.
			if (!rtpStream->ReceivePacket(packet))
			{
				// May have to announce a new RTP stream to the listener.
				if (this->mapSsrcRtpStream.size() > numRtpStreamsBefore)
				{
					NotifyNewRtpStream(rtpStream);
				}

				return result;
			}
		}
		// RTX packet.
		else if (packet->GetSsrc() == rtpStream->GetRtxSsrc())
		{
			result = ReceiveRtpPacketResult::RETRANSMISSION;
			isRtx  = true;

			// Process the packet.
			if (!rtpStream->ReceiveRtxPacket(packet))
			{
				return result;
			}
		}
		// Should not happen.
		else
		{
			MS_ABORT("found stream does not match received packet");
		}

		if (packet->IsKeyFrame())
		{
			MS_DEBUG_TAG(
			  rtp,
			  "key frame received [ssrc:%" PRIu32 ", seq:%" PRIu16 "]",
			  packet->GetSsrc(),
			  packet->GetSequenceNumber());

			// Tell the keyFrameRequestManager.
			if (this->keyFrameRequestManager)
				this->keyFrameRequestManager->KeyFrameReceived(packet->GetSsrc());
		}

		// May have to announce a new RTP stream to the listener.
		if (this->mapSsrcRtpStream.size() > numRtpStreamsBefore)
		{
			// Request a key frame for this stream since we may have lost the first packets
			// (do not do it if this is a key frame).
			if (this->keyFrameRequestManager && !this->paused && !packet->IsKeyFrame())
			{
				this->keyFrameRequestManager->ForceKeyFrameNeeded(packet->GetSsrc());
			}

			// Update current packet.
			this->currentRtpPacket = packet;

			NotifyNewRtpStream(rtpStream);

			// Reset current packet.
			this->currentRtpPacket = nullptr;
		}

		// If paused stop here.
		if (this->paused)
		{
			return result;
		}

		// May emit 'trace' event.
		EmitTraceEventRtpAndKeyFrameTypes(packet, isRtx);

		// Mangle the packet before providing the listener with it.
		// 在向侦听器提供数据包之前，先将其弄乱。
		if (!MangleRtpPacket(packet, rtpStream))
		{
			return ReceiveRtpPacketResult::DISCARDED;
		}

		// Post-process the packet.
		// 处理视频 转的角度
		PostProcessRtpPacket(packet);
		// 所有的订阅的客户端媒体信息
		// 数据保持在每个接受客户端缓存中中了RtpSend
		this->listener->OnProducerRtpPacketReceived(this, packet);

		return result;
	}

	void Producer::ReceiveRtcpSenderReport(RTC::RTCP::SenderReport* report)
	{
		MS_TRACE();

		auto it = this->mapSsrcRtpStream.find(report->GetSsrc());

		if (it != this->mapSsrcRtpStream.end())
		{
			auto* rtpStream = it->second;
			bool first      = rtpStream->GetSenderReportNtpMs() == 0;

			rtpStream->ReceiveRtcpSenderReport(report);

			this->listener->OnProducerRtcpSenderReport(this, rtpStream, first);

			return;
		}

		// If not found, check with RTX.
		auto it2 = this->mapRtxSsrcRtpStream.find(report->GetSsrc());

		if (it2 != this->mapRtxSsrcRtpStream.end())
		{
			auto* rtpStream = it2->second;

			rtpStream->ReceiveRtxRtcpSenderReport(report);

			return;
		}

		MS_DEBUG_TAG(rtcp, "RtpStream not found [ssrc:%" PRIu32 "]", report->GetSsrc());
	}

	void Producer::ReceiveRtcpXrDelaySinceLastRr(RTC::RTCP::DelaySinceLastRr::SsrcInfo* ssrcInfo)
	{
		MS_TRACE();

		auto it = this->mapSsrcRtpStream.find(ssrcInfo->GetSsrc());

		if (it == this->mapSsrcRtpStream.end())
		{
			MS_WARN_TAG(rtcp, "RtpStream not found [ssrc:%" PRIu32 "]", ssrcInfo->GetSsrc());

			return;
		}

		auto* rtpStream = it->second;

		rtpStream->ReceiveRtcpXrDelaySinceLastRr(ssrcInfo);
	}

	void Producer::GetRtcp(RTC::RTCP::CompoundPacket* packet, uint64_t nowMs)
	{
		MS_TRACE();

		if (static_cast<float>((nowMs - this->lastRtcpSentTime) * 1.15) < this->maxRtcpInterval)
		{
			return;
		}

		for (auto& kv : this->mapSsrcRtpStream)
		{
			RTC::RtpStreamRecv*			rtpStream = kv.second;
			RTC::RTCP::ReceiverReport*	report    = rtpStream->GetRtcpReceiverReport();

			packet->AddReceiverReport(report);

			RTC::RTCP::ReceiverReport*	rtxReport = rtpStream->GetRtxRtcpReceiverReport();

			if (rtxReport)
			{
				packet->AddReceiverReport(rtxReport);
			}
		}

		// Add a receiver reference time report if no present in the packet.
		if (!packet->HasReceiverReferenceTime())
		{
			Utils::Time::Ntp				    ntp     = Utils::Time::TimeMs2Ntp(nowMs);
			RTC::RTCP::ReceiverReferenceTime* report	= new RTC::RTCP::ReceiverReferenceTime();

			report->SetNtpSec(ntp.seconds);
			report->SetNtpFrac(ntp.fractions);
			packet->AddReceiverReferenceTime(report);
		}

		this->lastRtcpSentTime = nowMs;
	}

	void Producer::RequestKeyFrame(uint32_t mappedSsrc)
	{
		MS_TRACE();

		if (!this->keyFrameRequestManager || this->paused)
			return;

		auto it = this->mapMappedSsrcSsrc.find(mappedSsrc);

		if (it == this->mapMappedSsrcSsrc.end())
		{
			MS_WARN_2TAGS(rtcp, rtx, "given mappedSsrc not found, ignoring");

			return;
		}

		uint32_t ssrc = it->second;

		// If the current RTP packet is a key frame for the given mapped SSRC do
		// nothing since we are gonna provide Consumers with the requested key frame
		// right now.
		//
		// NOTE: We know that this may only happen before calling MangleRtpPacket()
		// so the SSRC of the packet is still the original one and not the mapped one.
		//
		// clang-format off
		if (
			this->currentRtpPacket &&
			this->currentRtpPacket->GetSsrc() == ssrc &&
			this->currentRtpPacket->IsKeyFrame()
		)
		// clang-format on
		{
			return;
		}

		this->keyFrameRequestManager->KeyFrameNeeded(ssrc);
	}

	RTC::RtpStreamRecv* Producer::GetRtpStream(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		uint32_t ssrc       = packet->GetSsrc();
		uint8_t payloadType = packet->GetPayloadType();

		// If stream found in media ssrcs map, return it.
		{
			auto it = this->mapSsrcRtpStream.find(ssrc);

			if (it != this->mapSsrcRtpStream.end())
			{
				auto* rtpStream = it->second;

				return rtpStream;
			}
		}

		// If stream found in RTX ssrcs map, return it.
		// 这边会看map中是否有ssrc中RTX中重传数据ssrc
		{
			auto it = this->mapRtxSsrcRtpStream.find(ssrc);

			if (it != this->mapRtxSsrcRtpStream.end())
			{
				auto* rtpStream = it->second;

				return rtpStream;
			}
		}

		// Otherwise check our encodings and, if appropriate, create a new stream.

		// First, look for an encoding with matching media or RTX ssrc value.
		for (size_t i{ 0 }; i < this->rtpParameters.encodings.size(); ++i)
		{
			auto& encoding         = this->rtpParameters.encodings[i];
			const auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);
			const auto* rtxCodec   = this->rtpParameters.GetRtxCodecForEncoding(encoding);
			bool isMediaPacket     = (mediaCodec->payloadType == payloadType);
			bool isRtxPacket       = (rtxCodec && rtxCodec->payloadType == payloadType);

			if (isMediaPacket && encoding.ssrc == ssrc)
			{
				auto* rtpStream = CreateRtpStream(packet, *mediaCodec, i);

				return rtpStream;
			}
			else if (isRtxPacket && encoding.hasRtx && encoding.rtx.ssrc == ssrc)
			{
				////////////////////////////////////RTX/////////////////////////////////////////////////////
				//一、什么是 RTX
				//	RTX 就是重传 Retransmission, 将丢失的包重新由发送方传给接收方。
				//
				//	Webrtc 默认开启 RTX (重传)，它一般采用不同的 SSRC 进行传输，即原始的 RTP 包和重传的 RTP 包的 SSRC 是不同的，这样不会干扰原始 RTP 包的度量。
				//
				//	RTX 包的 Payload 在 RFC4588 中有详细描述，一般 NACK 导致的重传包和 Bandwidth Probe 导致的探测包也可能走 RTX 通道。
				//
				//
				//	为什么用 RTX
				//	媒体流多使用 RTP 通过 UDP 进行传输，由于是不可靠传输，丢包是不可避免，也是可以容忍的，但是对于一些关键数据是不能丢失的，这时候就需要重传(RTX)。
				//
				//二、在 WebRTC 中常用的 QoS 策略有
				//
				//	反馈：例如 PLI , NACK
				//	冗余， 例如 FEC, RTX
				//	调整：例如码率，分辨率及帧率的调整
				//	缓存: 例如 Receive Adaptive Jitter Buffer, Send Buffer
				//	这些措施一般需要结合基于拥塞控制(congestion control) 及带宽估计(bandwidth estimation)技术, 不同的网络条件下需要采用不同的措施。
				//
				//	FEC 用作丢包恢复需要占用更多的带宽，即使 5% 的丢包需要几乎一倍的带宽，在带宽有限的情况下可能会使情况更糟。
				//
				//	RTX 不会占用太多的带宽，接收方发送 NACK 指明哪些包丢失了，发送方通过单独的 RTP 流（不同的 SSRC）来重传丢失的包，但是 RTX 至少需要一个 RTT 来修复丢失的包。
				//
				//	音频流对于延迟很敏感，而且占用带宽不多，所以用 FEC 更好。WebRTC 默认并不为 audio 开启 RTX
				//	视频流对于延迟没那么敏感，而且占用带宽很多，所以用 RTX 更好。
				/////////////////////////////////////////////////////////////////////////////////////////
				auto it = this->mapSsrcRtpStream.find(encoding.ssrc);

				// Ignore if no stream has been created yet for the corresponding encoding.
				if (it == this->mapSsrcRtpStream.end())
				{
					MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet for not yet created RtpStream (ssrc lookup)");

					return nullptr;
				}

				auto* rtpStream = it->second;

				// Ensure no RTX ssrc was previously detected.
				// 确保之前未检测到RTX ssrc
				if (rtpStream->HasRtx())
				{
					MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet with new ssrc (ssrc lookup)");

					return nullptr;
				}

				// Update the stream RTX data.
				rtpStream->SetRtx(payloadType, ssrc);

				// Insert the new RTX ssrc into the map.
				// TODO@chensong 20220909 怎么处理RTX的视频包重传数据呢
				this->mapRtxSsrcRtpStream[ssrc] = rtpStream;

				return rtpStream;
			}
		}

		// If not found, look for an encoding matching the packet RID value.
		std::string rid;

		if (packet->ReadRid(rid))
		{
			for (size_t i{ 0 }; i < this->rtpParameters.encodings.size(); ++i)
			{
				auto& encoding = this->rtpParameters.encodings[i];

				if (encoding.rid != rid)
					continue;

				const auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);
				const auto* rtxCodec   = this->rtpParameters.GetRtxCodecForEncoding(encoding);
				bool isMediaPacket     = (mediaCodec->payloadType == payloadType);
				bool isRtxPacket       = (rtxCodec && rtxCodec->payloadType == payloadType);

				if (isMediaPacket)
				{
					// Ensure no other stream already exists with same RID.
					for (auto& kv : this->mapSsrcRtpStream)
					{
						auto* rtpStream = kv.second;

						if (rtpStream->GetRid() == rid)
						{
							MS_WARN_TAG(
							  rtp, "ignoring packet with unknown ssrc but already handled RID (RID lookup)");

							return nullptr;
						}
					}

					auto* rtpStream = CreateRtpStream(packet, *mediaCodec, i);

					return rtpStream;
				}
				else if (isRtxPacket)
				{
					// Ensure a stream already exists with same RID.
					for (auto& kv : this->mapSsrcRtpStream)
					{
						auto* rtpStream = kv.second;

						if (rtpStream->GetRid() == rid)
						{
							// Ensure no RTX ssrc was previously detected.
							if (rtpStream->HasRtx())
							{
								MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet with new SSRC (RID lookup)");

								return nullptr;
							}

							// Update the stream RTX data.
							rtpStream->SetRtx(payloadType, ssrc);

							// Insert the new RTX ssrc into the map.
							this->mapRtxSsrcRtpStream[ssrc] = rtpStream;

							return rtpStream;
						}
					}

					MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet for not yet created RtpStream (RID lookup)");

					return nullptr;
				}
			}

			MS_WARN_TAG(rtp, "ignoring packet with unknown RID (RID lookup)");

			return nullptr;
		}

		// If not found, and there is a single encoding without ssrc and RID, this
		// may be the media or RTX stream.
		//
		// clang-format off
		if (
			this->rtpParameters.encodings.size() == 1 &&
			!this->rtpParameters.encodings[0].ssrc &&
			this->rtpParameters.encodings[0].rid.empty()
		)
		// clang-format on
		{
			auto& encoding         = this->rtpParameters.encodings[0];
			const auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);
			const auto* rtxCodec   = this->rtpParameters.GetRtxCodecForEncoding(encoding);
			bool isMediaPacket     = (mediaCodec->payloadType == payloadType);
			bool isRtxPacket       = (rtxCodec && rtxCodec->payloadType == payloadType);

			if (isMediaPacket)
			{
				// Ensure there is no other RTP stream already.
				if (!this->mapSsrcRtpStream.empty())
				{
					MS_WARN_TAG(
					  rtp,
					  "ignoring packet with unknown ssrc not matching the already existing stream (single RtpStream lookup)");

					return nullptr;
				}

				auto* rtpStream = CreateRtpStream(packet, *mediaCodec, 0);

				return rtpStream;
			}
			else if (isRtxPacket)
			{
				// There must be already a media RTP stream.
				auto it = this->mapSsrcRtpStream.begin();

				if (it == this->mapSsrcRtpStream.end())
				{
					MS_DEBUG_2TAGS(
					  rtp, rtx, "ignoring RTX packet for not yet created RtpStream (single stream lookup)");

					return nullptr;
				}

				auto* rtpStream = it->second;

				// Ensure no RTX SSRC was previously detected.
				if (rtpStream->HasRtx())
				{
					MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet with new SSRC (single stream lookup)");

					return nullptr;
				}

				// Update the stream RTX data.
				rtpStream->SetRtx(payloadType, ssrc);

				// Insert the new RTX SSRC into the map.
				this->mapRtxSsrcRtpStream[ssrc] = rtpStream;

				return rtpStream;
			}
		}

		return nullptr;
	}

	RTC::RtpStreamRecv* Producer::CreateRtpStream(
	  RTC::RtpPacket* packet, const RTC::RtpCodecParameters& mediaCodec, size_t encodingIdx)
	{
		MS_TRACE();

		uint32_t ssrc = packet->GetSsrc();

		MS_ASSERT(
		  this->mapSsrcRtpStream.find(ssrc) == this->mapSsrcRtpStream.end(),
		  "RtpStream with given SSRC already exists");
		MS_ASSERT(
		  !this->rtpStreamByEncodingIdx[encodingIdx],
		  "RtpStream for given encoding index already exists");

		auto& encoding        = this->rtpParameters.encodings[encodingIdx];
		auto& encodingMapping = this->rtpMapping.encodings[encodingIdx];

		MS_DEBUG_TAG(
		  rtp,
		  "[encodingIdx:%zu, ssrc:%" PRIu32 ", rid:%s, payloadType:%" PRIu8 "]",
		  encodingIdx,
		  ssrc,
		  encoding.rid.c_str(),
		  mediaCodec.payloadType);

		// Set stream params.
		RTC::RtpStream::Params params;

		params.encodingIdx    = encodingIdx;
		params.ssrc           = ssrc;
		params.payloadType    = mediaCodec.payloadType;
		params.mimeType       = mediaCodec.mimeType;
		params.clockRate      = mediaCodec.clockRate;
		params.rid            = encoding.rid;
		params.cname          = this->rtpParameters.rtcp.cname;
		params.spatialLayers  = encoding.spatialLayers;
		params.temporalLayers = encoding.temporalLayers;

		// Check in band FEC in codec parameters.
		if (mediaCodec.parameters.HasInteger("useinbandfec") && mediaCodec.parameters.GetInteger("useinbandfec") == 1)
		{
			MS_DEBUG_TAG(rtcp, "in band FEC enabled");

			params.useInBandFec = true;
		}

		// Check DTX in codec parameters.
		if (mediaCodec.parameters.HasInteger("usedtx") && mediaCodec.parameters.GetInteger("usedtx") == 1)
		{
			MS_DEBUG_TAG(rtp, "DTX enabled");

			params.useDtx = true;
		}

		// Check DTX in the encoding.
		if (encoding.dtx)
		{
			MS_DEBUG_TAG(rtp, "DTX enabled");

			params.useDtx = true;
		}

		for (const auto& fb : mediaCodec.rtcpFeedback)
		{
			if (!params.useNack && fb.type == "nack" && fb.parameter.empty())
			{
				MS_DEBUG_2TAGS(rtp, rtcp, "NACK supported");

				params.useNack = true;
			}
			else if (!params.usePli && fb.type == "nack" && fb.parameter == "pli")
			{
				MS_DEBUG_2TAGS(rtp, rtcp, "PLI supported");

				params.usePli = true;
			}
			else if (!params.useFir && fb.type == "ccm" && fb.parameter == "fir")
			{
				MS_DEBUG_2TAGS(rtp, rtcp, "FIR supported");

				params.useFir = true;
			}
		}

		// Create a RtpStreamRecv for receiving a media stream.
		auto* rtpStream = new RTC::RtpStreamRecv(this, params);

		// Insert into the maps.
		this->mapSsrcRtpStream[ssrc]              = rtpStream;
		this->rtpStreamByEncodingIdx[encodingIdx] = rtpStream;
		this->rtpStreamScores[encodingIdx]        = rtpStream->GetScore();

		// Set the mapped SSRC.

		this->mapRtpStreamMappedSsrc[rtpStream]             = encodingMapping.mappedSsrc;
		this->mapMappedSsrcSsrc[encodingMapping.mappedSsrc] = ssrc;

		// If the Producer is paused tell it to the new RtpStreamRecv.
		if (this->paused)
		{
			rtpStream->Pause();
		}

		// Emit the first score event right now.
		EmitScore();

		return rtpStream;
	}

	void Producer::NotifyNewRtpStream(RTC::RtpStreamRecv* rtpStream)
	{
		MS_TRACE();

		auto mappedSsrc = this->mapRtpStreamMappedSsrc.at(rtpStream);

		// Notify the listener.
		this->listener->OnProducerNewRtpStream(this, static_cast<RTC::RtpStream*>(rtpStream), mappedSsrc);
	}

	inline void Producer::PreProcessRtpPacket(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		if (this->kind == RTC::Media::Kind::VIDEO)
		{
			// NOTE: Remove this once framemarking draft becomes RFC.
			packet->SetFrameMarking07ExtensionId(this->rtpHeaderExtensionIds.frameMarking07);
			packet->SetFrameMarkingExtensionId(this->rtpHeaderExtensionIds.frameMarking);
		}
	}

	inline bool Producer::MangleRtpPacket(RTC::RtpPacket* packet, RTC::RtpStreamRecv* rtpStream) const
	{
		MS_TRACE();

		// Mangle the payload type.
		
		{
			// 1、 修改推流端编码id映射表 config.js中codecid表
			uint8_t payloadType = packet->GetPayloadType();
			auto it             = this->rtpMapping.codecs.find(payloadType);

			if (it == this->rtpMapping.codecs.end())
			{
				MS_WARN_TAG(rtp, "unknown payload type [payloadType:%" PRIu8 "]", payloadType);

				return false;
			}

			uint8_t mappedPayloadType = it->second;

			packet->SetPayloadType(mappedPayloadType);
		}

		// Mangle the SSRC.
		{
			// 2.  修改为服务端ssrc   这个可能是因为不同端可能会冲突的问题吧
			uint32_t mappedSsrc = this->mapRtpStreamMappedSsrc.at(rtpStream);

			packet->SetSsrc(mappedSsrc);
		}

		// Mangle RTP header extensions.
		{
			thread_local static uint8_t buffer[4096];
			thread_local static std::vector<RTC::RtpPacket::GenericExtension> extensions;

			// This happens just once.
			if (extensions.capacity() != 24)
			{
				extensions.reserve(24);
			}

			extensions.clear();

			uint8_t* extenValue;
			uint8_t extenLen;
			uint8_t* bufferPtr{ buffer };

			// Add urn:ietf:params:rtp-hdrext:sdes:mid.
			{
				extenLen = RTC::MidMaxLength;

				extensions.emplace_back(
				  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::MID), extenLen, bufferPtr);

				bufferPtr += extenLen;
			}

			if (this->kind == RTC::Media::Kind::AUDIO)
			{
				// Proxy urn:ietf:params:rtp-hdrext:ssrc-audio-level.
				extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.ssrcAudioLevel, extenLen);

				if (extenValue)
				{
					std::memcpy(bufferPtr, extenValue, extenLen);

					extensions.emplace_back(
					  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::SSRC_AUDIO_LEVEL),
					  extenLen,
					  bufferPtr);

					// Not needed since this is the latest added extension.
					// bufferPtr += extenLen;
				}
			}
			else if (this->kind == RTC::Media::Kind::VIDEO)
			{
				// Add http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time.
				{
					extenLen = 3u;

					// NOTE: Add value 0. The sending Transport will update it.
					uint32_t absSendTime{ 0u };

					Utils::Byte::Set3Bytes(bufferPtr, 0, absSendTime);

					extensions.emplace_back(
					  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::ABS_SEND_TIME), extenLen, bufferPtr);

					bufferPtr += extenLen;
				}

				// Add http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01.
				{
					extenLen = 2u;

					// NOTE: Add value 0. The sending Transport will update it.
					uint16_t wideSeqNumber{ 0u };

					Utils::Byte::Set2Bytes(bufferPtr, 0, wideSeqNumber);

					extensions.emplace_back(
					  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01),
					  extenLen,
					  bufferPtr);

					bufferPtr += extenLen;
				}

				// NOTE: Remove this once framemarking draft becomes RFC.
				// Proxy http://tools.ietf.org/html/draft-ietf-avtext-framemarking-07.
				extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.frameMarking07, extenLen);

				if (extenValue)
				{
					std::memcpy(bufferPtr, extenValue, extenLen);

					extensions.emplace_back(
					  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING_07),
					  extenLen,
					  bufferPtr);

					bufferPtr += extenLen;
				}

				// Proxy urn:ietf:params:rtp-hdrext:framemarking.
				extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.frameMarking, extenLen);

				if (extenValue)
				{
					std::memcpy(bufferPtr, extenValue, extenLen);

					extensions.emplace_back(
					  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING), extenLen, bufferPtr);

					bufferPtr += extenLen;
				}

				// Proxy urn:3gpp:video-orientation.
				extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.videoOrientation, extenLen);

				if (extenValue)
				{
					std::memcpy(bufferPtr, extenValue, extenLen);

					extensions.emplace_back(
					  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::VIDEO_ORIENTATION),
					  extenLen,
					  bufferPtr);

					bufferPtr += extenLen;
				}

				// Proxy urn:ietf:params:rtp-hdrext:toffset.
				extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.toffset, extenLen);

				if (extenValue)
				{
					std::memcpy(bufferPtr, extenValue, extenLen);

					extensions.emplace_back(
					  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::TOFFSET), extenLen, bufferPtr);

					// Not needed since this is the latest added extension.
					// bufferPtr += extenLen;
				}
			}

			// Set the new extensions into the packet using One-Byte format.
			packet->SetExtensions(1, extensions);

			// Assign mediasoup RTP header extension ids (just those that mediasoup may
			// be interested in after passing it to the Router).
			packet->SetMidExtensionId(static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::MID));
			packet->SetAbsSendTimeExtensionId(
			  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::ABS_SEND_TIME));
			packet->SetTransportWideCc01ExtensionId(
			  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01));
			// NOTE: Remove this once framemarking draft becomes RFC.
			packet->SetFrameMarking07ExtensionId(
			  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING_07));
			packet->SetFrameMarkingExtensionId(
			  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING));
			packet->SetSsrcAudioLevelExtensionId(
			  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::SSRC_AUDIO_LEVEL));
			packet->SetVideoOrientationExtensionId(
			  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::VIDEO_ORIENTATION));
		}

		return true;
	}

	inline void Producer::PostProcessRtpPacket(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		if (this->kind == RTC::Media::Kind::VIDEO)
		{
			bool camera;
			bool flip;
			uint16_t rotation;

			if (packet->ReadVideoOrientation(camera, flip, rotation))
			{
				// If video orientation was not yet detected or any value has changed,
				// emit event.
				// clang-format off
				if (
					!this->videoOrientationDetected ||
					camera != this->videoOrientation.camera ||
					flip != this->videoOrientation.flip ||
					rotation != this->videoOrientation.rotation
				)
				// clang-format on
				{
					this->videoOrientationDetected  = true;
					this->videoOrientation.camera   = camera;
					this->videoOrientation.flip     = flip;
					this->videoOrientation.rotation = rotation;

					json data = json::object();

					data["camera"]   = this->videoOrientation.camera;
					data["flip"]     = this->videoOrientation.flip;
					data["rotation"] = this->videoOrientation.rotation;

					Channel::ChannelNotifier::Emit(this->id, "videoorientationchange", data);
				}
			}
		}
	}

	inline void Producer::EmitScore() const
	{
		MS_TRACE();

		json data = json::array();

		for (auto* rtpStream : this->rtpStreamByEncodingIdx)
		{
			if (!rtpStream)
				continue;

			data.emplace_back(json::value_t::object);

			auto& jsonEntry = data[data.size() - 1];

			jsonEntry["encodingIdx"] = rtpStream->GetEncodingIdx();
			jsonEntry["ssrc"]        = rtpStream->GetSsrc();

			if (!rtpStream->GetRid().empty())
				jsonEntry["rid"] = rtpStream->GetRid();

			jsonEntry["score"] = rtpStream->GetScore();
		}

		Channel::ChannelNotifier::Emit(this->id, "score", data);
	}

	inline void Producer::EmitTraceEventRtpAndKeyFrameTypes(RTC::RtpPacket* packet, bool isRtx) const
	{
		MS_TRACE();

		if (this->traceEventTypes.keyframe && packet->IsKeyFrame())
		{
			json data = json::object();

			data["type"]      = "keyframe";
			data["timestamp"] = DepLibUV::GetTimeMs();
			data["direction"] = "in";

			packet->FillJson(data["info"]);

			if (isRtx)
				data["info"]["isRtx"] = true;

			Channel::ChannelNotifier::Emit(this->id, "trace", data);
		}
		else if (this->traceEventTypes.rtp)
		{
			json data = json::object();

			data["type"]      = "rtp";
			data["timestamp"] = DepLibUV::GetTimeMs();
			data["direction"] = "in";

			packet->FillJson(data["info"]);

			if (isRtx)
				data["info"]["isRtx"] = true;

			Channel::ChannelNotifier::Emit(this->id, "trace", data);
		}
	}

	inline void Producer::EmitTraceEventPliType(uint32_t ssrc) const
	{
		MS_TRACE();

		if (!this->traceEventTypes.pli)
			return;

		json data = json::object();

		data["type"]         = "pli";
		data["timestamp"]    = DepLibUV::GetTimeMs();
		data["direction"]    = "out";
		data["info"]["ssrc"] = ssrc;

		Channel::ChannelNotifier::Emit(this->id, "trace", data);
	}

	inline void Producer::EmitTraceEventFirType(uint32_t ssrc) const
	{
		MS_TRACE();

		if (!this->traceEventTypes.fir)
			return;

		json data = json::object();

		data["type"]         = "fir";
		data["timestamp"]    = DepLibUV::GetTimeMs();
		data["direction"]    = "out";
		data["info"]["ssrc"] = ssrc;

		Channel::ChannelNotifier::Emit(this->id, "trace", data);
	}

	inline void Producer::EmitTraceEventNackType() const
	{
		MS_TRACE();

		if (!this->traceEventTypes.nack)
			return;

		json data = json::object();

		data["type"]      = "nack";
		data["timestamp"] = DepLibUV::GetTimeMs();
		data["direction"] = "out";
		data["info"]      = json::object();

		Channel::ChannelNotifier::Emit(this->id, "trace", data);
	}

	inline void Producer::OnRtpStreamScore(RTC::RtpStream* rtpStream, uint8_t score, uint8_t previousScore)
	{
		MS_TRACE();

		// Update the vector of scores.
		this->rtpStreamScores[rtpStream->GetEncodingIdx()] = score;

		// Notify the listener.
		this->listener->OnProducerRtpStreamScore(this, rtpStream, score, previousScore);

		// Emit the score event.
		EmitScore();
	}

	inline void Producer::OnRtpStreamSendRtcpPacket(
	  RTC::RtpStreamRecv* /*rtpStream*/, RTC::RTCP::Packet* packet)
	{
		switch (packet->GetType())
		{
			case RTC::RTCP::Type::PSFB:
			{
				auto* feedback = static_cast<RTC::RTCP::FeedbackPsPacket*>(packet);

				switch (feedback->GetMessageType())
				{
					case RTC::RTCP::FeedbackPs::MessageType::PLI:
					{
						// May emit 'trace' event.
						EmitTraceEventPliType(feedback->GetMediaSsrc());

						break;
					}

					case RTC::RTCP::FeedbackPs::MessageType::FIR:
					{
						// May emit 'trace' event.
						EmitTraceEventFirType(feedback->GetMediaSsrc());

						break;
					}

					default:;
				}
			}

			case RTC::RTCP::Type::RTPFB:
			{
				auto* feedback = static_cast<RTC::RTCP::FeedbackRtpPacket*>(packet);

				switch (feedback->GetMessageType())
				{
					case RTC::RTCP::FeedbackRtp::MessageType::NACK:
					{
						// May emit 'trace' event.
						EmitTraceEventNackType();

						break;
					}

					default:;
				}
			}

			default:;
		}

		// Notify the listener.
		this->listener->OnProducerSendRtcpPacket(this, packet);
	}

	inline void Producer::OnRtpStreamNeedWorstRemoteFractionLost(
	  RTC::RtpStreamRecv* rtpStream, uint8_t& worstRemoteFractionLost)
	{
		auto mappedSsrc = this->mapRtpStreamMappedSsrc.at(rtpStream);

		// Notify the listener.
		this->listener->OnProducerNeedWorstRemoteFractionLost(this, mappedSsrc, worstRemoteFractionLost);
	}

	inline void Producer::OnKeyFrameNeeded(
	  RTC::KeyFrameRequestManager* /*keyFrameRequestManager*/, uint32_t ssrc)
	{
		MS_TRACE();

		auto it = this->mapSsrcRtpStream.find(ssrc);

		if (it == this->mapSsrcRtpStream.end())
		{
			MS_WARN_2TAGS(rtcp, rtx, "no associated RtpStream found [ssrc:%" PRIu32 "]", ssrc);

			return;
		}

		auto* rtpStream = it->second;

		rtpStream->RequestKeyFrame();
	}
} // namespace RTC
