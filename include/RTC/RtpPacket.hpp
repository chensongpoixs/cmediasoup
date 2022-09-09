#ifndef MS_RTC_RTP_PACKET_HPP
#define MS_RTC_RTP_PACKET_HPP

#include "common.hpp"
#include "Utils.hpp"
#include "RTC/Codecs/PayloadDescriptorHandler.hpp"
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace RTC
{
	// Max RTP length.
	constexpr size_t RtpBufferSize{ 65536u };
	// Max MTU size.
	constexpr size_t MtuSize{ 1500u };
	// MID header extension max length (just used when setting/updating MID
	// extension).
	constexpr uint8_t MidMaxLength{ 8u };

	class RtpPacket
	{
	public:
		/* Struct for RTP header. */
		struct Header
		{
#if defined(MS_LITTLE_ENDIAN)
			uint8_t csrcCount : 4;
			uint8_t extension : 1;
			uint8_t padding : 1;
			uint8_t version : 2;
			uint8_t payloadType : 7;
			uint8_t marker : 1;
#elif defined(MS_BIG_ENDIAN)
			uint8_t version : 2;
			uint8_t padding : 1;
			uint8_t extension : 1;
			uint8_t csrcCount : 4;
			uint8_t marker : 1;
			uint8_t payloadType : 7;
#endif
			uint16_t sequenceNumber;
			uint32_t timestamp;
			uint32_t ssrc;
		};

	private:
		/* Struct for RTP header extension. */
		struct HeaderExtension
		{
			uint16_t id;
			uint16_t length; // Size of value in multiples of 4 bytes.
			uint8_t value[1];
		};

	private:
		/* Struct for One-Byte extension. */
		struct OneByteExtension
		{
#if defined(MS_LITTLE_ENDIAN)
			uint8_t len : 4;
			uint8_t id : 4;
#elif defined(MS_BIG_ENDIAN)
			uint8_t id : 4;
			uint8_t len : 4;
#endif
			uint8_t value[1];
		};

	private:
		/* Struct for Two-Bytes extension. */
		struct TwoBytesExtension
		{
			uint8_t id : 8;
			uint8_t len : 8;
			uint8_t value[1];
		};

	public:
		/* Struct for replacing and setting header extensions. */
		struct GenericExtension
		{
			GenericExtension(uint8_t id, uint8_t len, uint8_t* value) : id(id), len(len), value(value){};

			uint8_t id : 8;
			uint8_t len : 8;
			uint8_t* value;
		};

	public:
		/* Struct with frame-marking information. */
		
// Frame Marking.
//
// Meta-information about an RTP stream outside the encrypted media payload,
// useful for an RTP switch to do codec-agnostic selective forwarding
// without decrypting the payload.
//
// For non-scalable streams:
//    0                   1
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |  ID   | L = 0 |S|E|I|D|0 0 0 0|
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// For scalable streams:
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |  ID   | L = 2 |S|E|I|D|B| TID |      LID      |   TL0PICIDX   |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
///////////////////////////////////////////////////////////////////////////////////////
//	各个字段解释如下：
//	
//		S				: Start of Frame(1 bit) ，一帧的开始。 
//		E				: End of Frame(1 bit) ，一帧的结束。 
//		I				: Independent Frame(1 bit) ，是否是独立可解码的一帧，如264的IDR。 
//		D				: Discardable Frame(1 bit) ，可以被丢弃的帧，如时域分层中high layer，丢弃后其他帧仍然能正常解码。 
//		B				: Base Layer Sync(1 bit) ，当TID不为0，发送端知道该帧只依赖base layer，则填1，否则填0
//	    TID				: Temporal ID(3 bits) ，时域分层中的层ID，0表示base layer。短的extension格式中，填0.
//		LID				: Layer ID(8 bits) ，空域或者质量分层的层ID，0标识base layer。 
//		TL0PICIDX		: Temporal Layer 0 Picture Index(8 bits) ，时域0层picture index。
//						  因为只有8个bit，所以是循环计数的。TID = 0 LID = 0标识该帧ID，TID != 0或者LID != 0标识该帧依赖哪一个picture index。
//						  如果不存在时域分层或者计数未知，这个字段可以省略。
//	
///////////////////////////////////////////////////////////////////////////////////////

		struct FrameMarking
		{
#if defined(MS_LITTLE_ENDIAN)
			uint8_t tid : 3;
			uint8_t base : 1;
			uint8_t discardable : 1; //可以被丢弃的帧，如时域分层中high layer，丢弃后其他帧仍然能正常解码。 
			uint8_t independent : 1; // 是否是独立可解码的一帧，如264的IDR。 
			uint8_t end : 1;
			uint8_t start : 1;
#elif defined(MS_BIG_ENDIAN)
			uint8_t start : 1;
			uint8_t end : 1;
			uint8_t independent : 1;
			uint8_t discardable : 1;
			uint8_t base : 1;
			uint8_t tid : 3;
#endif
			uint8_t lid;
			uint8_t tl0picidx;
		};

	public:
		static bool IsRtp(const uint8_t* data, size_t len)
		{
			// NOTE: RtcpPacket::IsRtcp() must always be called before this method.

			auto header = const_cast<Header*>(reinterpret_cast<const Header*>(data));

			// clang-format off
			return (
				(len >= sizeof(Header)) &&
				// DOC: https://tools.ietf.org/html/draft-ietf-avtcore-rfc5764-mux-fixes
				(data[0] > 127 && data[0] < 192) &&
				// RTP Version must be 2.
				(header->version == 2)
			);
			// clang-format on
		}

		static RtpPacket* Parse(const uint8_t* data, size_t len);

	private:
		RtpPacket(
		  Header* header,
		  HeaderExtension* headerExtension,
		  const uint8_t* payload,
		  size_t payloadLength,
		  uint8_t payloadPadding,
		  size_t size);

	public:
		~RtpPacket();

		void Dump() const;

		void FillJson(json& jsonObject) const;

		const uint8_t* GetData() const
		{
			return (const uint8_t*)this->header;
		}

		size_t GetSize() const
		{
			return this->size;
		}

		uint8_t GetPayloadType() const
		{
			return this->header->payloadType;
		}

		void SetPayloadType(uint8_t payloadType)
		{
			this->header->payloadType = payloadType;
		}

		bool HasMarker() const
		{
			return this->header->marker;
		}

		void SetMarker(bool marker)
		{
			this->header->marker = marker;
		}

		void SetPayloadPaddingFlag(bool flag)
		{
			this->header->padding = flag;
		}

		uint16_t GetSequenceNumber() const
		{
			return uint16_t{ ntohs(this->header->sequenceNumber) };
		}

		void SetSequenceNumber(uint16_t seq)
		{
			this->header->sequenceNumber = uint16_t{ htons(seq) };
		}

		uint32_t GetTimestamp() const
		{
			return uint32_t{ ntohl(this->header->timestamp) };
		}

		void SetTimestamp(uint32_t timestamp)
		{
			this->header->timestamp = uint32_t{ htonl(timestamp) };
		}

		uint32_t GetSsrc() const
		{
			return uint32_t{ ntohl(this->header->ssrc) };
		}

		void SetSsrc(uint32_t ssrc)
		{
			this->header->ssrc = uint32_t{ htonl(ssrc) };
		}

		bool HasHeaderExtension() const
		{
			return (this->headerExtension ? true : false);
		}

		// After calling this method, all the extension ids are reset to 0.
		void SetExtensions(uint8_t type, const std::vector<GenericExtension>& extensions);

		uint16_t GetHeaderExtensionId() const
		{
			if (!this->headerExtension)
				return 0u;

			return uint16_t{ ntohs(this->headerExtension->id) };
		}

		size_t GetHeaderExtensionLength() const
		{
			if (!this->headerExtension)
				return 0u;

			return static_cast<size_t>(ntohs(this->headerExtension->length) * 4);
		}

		uint8_t* GetHeaderExtensionValue() const
		{
			if (!this->headerExtension)
				return nullptr;

			return this->headerExtension->value;
		}

		bool HasOneByteExtensions() const
		{
			return GetHeaderExtensionId() == 0xBEDE;
		}

		bool HasTwoBytesExtensions() const
		{
			return (GetHeaderExtensionId() & 0b1111111111110000) == 0b0001000000000000;
		}

		void SetMidExtensionId(uint8_t id)
		{
			this->midExtensionId = id;
		}

		void SetRidExtensionId(uint8_t id)
		{
			this->ridExtensionId = id;
		}

		void SetRepairedRidExtensionId(uint8_t id)
		{
			this->rridExtensionId = id;
		}

		void SetAbsSendTimeExtensionId(uint8_t id)
		{
			this->absSendTimeExtensionId = id;
		}

		void SetTransportWideCc01ExtensionId(uint8_t id)
		{
			this->transportWideCc01ExtensionId = id;
		}

		// NOTE: Remove once RFC.
		void SetFrameMarking07ExtensionId(uint8_t id)
		{
			this->frameMarking07ExtensionId = id;
		}

		void SetFrameMarkingExtensionId(uint8_t id)
		{
			this->frameMarkingExtensionId = id;
		}

		void SetSsrcAudioLevelExtensionId(uint8_t id)
		{
			this->ssrcAudioLevelExtensionId = id;
		}

		void SetVideoOrientationExtensionId(uint8_t id)
		{
			this->videoOrientationExtensionId = id;
		}

		bool ReadMid(std::string& mid) const
		{
			uint8_t extenLen;
			uint8_t* extenValue = GetExtension(this->midExtensionId, extenLen);

			if (!extenValue || extenLen == 0u)
				return false;

			mid.assign(reinterpret_cast<const char*>(extenValue), static_cast<size_t>(extenLen));

			return true;
		}

		bool UpdateMid(const std::string& mid);

		bool ReadRid(std::string& rid) const
		{
			// First try with the RID id then with the Repaired RID id.
			uint8_t extenLen;
			uint8_t* extenValue = GetExtension(this->ridExtensionId, extenLen);

			if (extenValue && extenLen > 0u)
			{
				rid.assign(reinterpret_cast<const char*>(extenValue), static_cast<size_t>(extenLen));

				return true;
			}

			extenValue = GetExtension(this->rridExtensionId, extenLen);

			if (extenValue && extenLen > 0u)
			{
				rid.assign(reinterpret_cast<const char*>(extenValue), static_cast<size_t>(extenLen));

				return true;
			}

			return false;
		}

		bool ReadAbsSendTime(uint32_t& absSendtime) const
		{
			uint8_t extenLen;
			uint8_t* extenValue = GetExtension(this->absSendTimeExtensionId, extenLen);

			if (!extenValue || extenLen != 3u)
				return false;

			absSendtime = Utils::Byte::Get3Bytes(extenValue, 0);

			return true;
		}

		bool UpdateAbsSendTime(uint64_t ms)
		{
			uint8_t extenLen;
			uint8_t* extenValue = GetExtension(this->absSendTimeExtensionId, extenLen);

			if (!extenValue || extenLen != 3u)
				return false;

			auto absSendTime = Utils::Time::TimeMsToAbsSendTime(ms);

			Utils::Byte::Set3Bytes(extenValue, 0, absSendTime);

			return true;
		}

		bool ReadTransportWideCc01(uint16_t& wideSeqNumber) const
		{
			uint8_t extenLen;
			uint8_t* extenValue = GetExtension(this->transportWideCc01ExtensionId, extenLen);

			if (!extenValue || extenLen != 2u)
				return false;

			wideSeqNumber = Utils::Byte::Get2Bytes(extenValue, 0);

			return true;
		}

		bool UpdateTransportWideCc01(uint16_t wideSeqNumber)
		{
			uint8_t extenLen;
			uint8_t* extenValue = GetExtension(this->transportWideCc01ExtensionId, extenLen);

			if (!extenValue || extenLen != 2u)
				return false;

			Utils::Byte::Set2Bytes(extenValue, 0, wideSeqNumber);

			return true;
		}

		bool ReadFrameMarking(RtpPacket::FrameMarking** frameMarking, uint8_t& length) const
		{
			uint8_t extenLen;
			uint8_t* extenValue = GetExtension(this->frameMarkingExtensionId, extenLen);

			// NOTE: Remove this once framemarking draft becomes RFC.
			if (!extenValue)
				extenValue = GetExtension(this->frameMarking07ExtensionId, extenLen);

			if (!extenValue || extenLen > 3u)
				return false;

			*frameMarking = reinterpret_cast<RtpPacket::FrameMarking*>(extenValue);
			length        = extenLen;

			return true;
		}

		bool ReadSsrcAudioLevel(uint8_t& volume, bool& voice) const
		{
			uint8_t extenLen;
			uint8_t* extenValue = GetExtension(this->ssrcAudioLevelExtensionId, extenLen);

			if (!extenValue || extenLen != 1u)
				return false;

			volume = Utils::Byte::Get1Byte(extenValue, 0);
			voice  = (volume & (1 << 7)) ? true : false;
			volume &= ~(1 << 7);

			return true;
		}

		bool ReadVideoOrientation(bool& camera, bool& flip, uint16_t& rotation) const
		{
			uint8_t extenLen;
			uint8_t* extenValue = GetExtension(this->videoOrientationExtensionId, extenLen);

			if (!extenValue || extenLen != 1u)
				return false;

			uint8_t cvoByte       = Utils::Byte::Get1Byte(extenValue, 0);
			uint8_t cameraValue   = ((cvoByte & 0b00001000) >> 3);
			uint8_t flipValue     = ((cvoByte & 0b00000100) >> 2);
			uint8_t rotationValue = (cvoByte & 0b00000011);

			camera = cameraValue ? true : false;
			flip   = flipValue ? true : false;

			// Using counter clockwise values.
			switch (rotationValue)
			{
				case 3:
					rotation = 270;
					break;
				case 2:
					rotation = 180;
					break;
				case 1:
					rotation = 90;
					break;
				default:
					rotation = 0;
			}

			return true;
		}

		bool HasExtension(uint8_t id) const
		{
			if (id == 0u)
			{
				return false;
			}
			else if (HasOneByteExtensions())
			{
				auto it = this->mapOneByteExtensions.find(id);

				return it != this->mapOneByteExtensions.end();
			}
			else if (HasTwoBytesExtensions())
			{
				auto it = this->mapTwoBytesExtensions.find(id);

				if (it == this->mapTwoBytesExtensions.end())
					return false;

				auto* extension = it->second;

				// In Two-Byte extensions value length may be zero. If so, return false.
				if (extension->len == 0u)
					return false;

				return true;
			}
			else
			{
				return false;
			}
		}

		uint8_t* GetExtension(uint8_t id, uint8_t& len) const
		{
			len = 0u;

			if (id == 0u)
			{
				return nullptr;
			}
			else if (HasOneByteExtensions())
			{
				auto it = this->mapOneByteExtensions.find(id);

				if (it == this->mapOneByteExtensions.end())
					return nullptr;

				auto* extension = it->second;

				// In One-Byte extensions value length 0 means 1.
				len = extension->len + 1;

				return extension->value;
			}
			else if (HasTwoBytesExtensions())
			{
				auto it = this->mapTwoBytesExtensions.find(id);

				if (it == this->mapTwoBytesExtensions.end())
					return nullptr;

				auto* extension = it->second;

				len = extension->len;

				// In Two-Byte extensions value length may be zero. If so, return nullptr.
				if (extension->len == 0u)
					return nullptr;

				return extension->value;
			}
			else
			{
				return nullptr;
			}
		}

		bool SetExtensionLength(uint8_t id, uint8_t len);

		uint8_t* GetPayload() const
		{
			return this->payloadLength != 0u ? this->payload : nullptr;
		}

		size_t GetPayloadLength() const
		{
			return this->payloadLength;
		}

		void SetPayloadLength(size_t length);

		uint8_t GetPayloadPadding() const
		{
			return this->payloadPadding;
		}

		uint8_t GetSpatialLayer() const
		{
			if (!this->payloadDescriptorHandler)
				return 0u;

			return this->payloadDescriptorHandler->GetSpatialLayer();
		}

		uint8_t GetTemporalLayer() const
		{
			if (!this->payloadDescriptorHandler)
				return 0u;

			return this->payloadDescriptorHandler->GetTemporalLayer();
		}

		bool IsKeyFrame() const
		{
			if (!this->payloadDescriptorHandler)
				return false;

			return this->payloadDescriptorHandler->IsKeyFrame();
		}

		RtpPacket* Clone(const uint8_t* buffer) const;

		void RtxEncode(uint8_t payloadType, uint32_t ssrc, uint16_t seq);

		bool RtxDecode(uint8_t payloadType, uint32_t ssrc);

		void SetPayloadDescriptorHandler(RTC::Codecs::PayloadDescriptorHandler* payloadDescriptorHandler)
		{
			this->payloadDescriptorHandler.reset(payloadDescriptorHandler);
		}

		bool ProcessPayload(RTC::Codecs::EncodingContext* context);

		void RestorePayload();

		void ShiftPayload(size_t payloadOffset, size_t shift, bool expand = true);

	private:
		void ParseExtensions();

	private:
		// Passed by argument.
		Header* header{ nullptr };
		uint8_t* csrcList{ nullptr };
		HeaderExtension* headerExtension{ nullptr };   // rtp中扩展头 
		std::map<uint8_t, OneByteExtension*> mapOneByteExtensions;   
		std::map<uint8_t, TwoBytesExtension*> mapTwoBytesExtensions;
		uint8_t midExtensionId{ 0u };
		uint8_t ridExtensionId{ 0u };
		uint8_t rridExtensionId{ 0u };
		uint8_t absSendTimeExtensionId{ 0u };
		uint8_t transportWideCc01ExtensionId{ 0u };
		uint8_t frameMarking07ExtensionId{ 0u }; // NOTE: Remove once RFC.
		uint8_t frameMarkingExtensionId{ 0u };
		uint8_t ssrcAudioLevelExtensionId{ 0u };
		uint8_t videoOrientationExtensionId{ 0u };
		uint8_t* payload{ nullptr };
		size_t payloadLength{ 0u };
		uint8_t payloadPadding{ 0u };
		size_t size{ 0u }; // Full size of the packet in bytes.
		// Codecs
		std::unique_ptr<Codecs::PayloadDescriptorHandler> payloadDescriptorHandler;
	};
} // namespace RTC

#endif
