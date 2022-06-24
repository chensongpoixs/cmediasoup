#ifndef MS_RTC_RATE_CALCULATOR_HPP
#define MS_RTC_RATE_CALCULATOR_HPP

#include "common.hpp"
#include "DepLibUV.hpp"
#include "RTC/RtpPacket.hpp"

namespace RTC
{
	// It is considered that the time source increases monotonically.
	// ie: the current timestamp can never be minor than a timestamp in the past.
	class RateCalculator
	{
	public:
		static constexpr size_t DefaultWindowSize{ 1000u };   // 默认滑动窗口占有毫秒数
		static constexpr float DefaultBpsScale{ 8000.0f };    // 比特流
		static constexpr uint16_t DefaultWindowItems{ 100u }; // 滑动窗口有多少个

	public:
		RateCalculator(
		  size_t windowSizeMs  = DefaultWindowSize,
		  float scale          = DefaultBpsScale,
		  uint16_t windowItems = DefaultWindowItems)
		  : windowSizeMs(windowSizeMs), scale(scale), windowItems(windowItems)
		{
			this->itemSizeMs = std::max(windowSizeMs / windowItems, static_cast<size_t>(1));

			Reset();
		}
		void Update(size_t size, uint64_t nowMs);
		uint32_t GetRate(uint64_t nowMs);
		size_t GetBytes() const
		{
			return this->bytes;
		}

	private:
		void RemoveOldData(uint64_t nowMs);
		void Reset()
		{
			this->buffer.reset(new BufferItem[this->windowItems]);
			this->newestItemStartTime = 0u;
			this->newestItemIndex     = -1;
			this->oldestItemStartTime = 0u;
			this->oldestItemIndex     = -1;
			this->totalCount          = 0u;
			this->lastRate            = 0u;
			this->lastTime            = 0u;
		}

	private:
		struct BufferItem
		{
			size_t count{ 0u };
			uint64_t time{ 0u };
		};

	private:
		// Window Size (in milliseconds).
		size_t windowSizeMs{ DefaultWindowSize }; // 滑动窗口的大小毫秒数
		// Scale in which the rate is represented.
		float scale{ DefaultBpsScale }; // 表示比率的刻度。
		// Window Size (number of items).
		uint16_t windowItems{ DefaultWindowItems }; // 滑动窗口的数组个数
		// Item Size (in milliseconds), calculated as: windowSizeMs / windowItems.
		size_t itemSizeMs{ 0u }; // 每个滑动窗口占有毫秒数
		// Buffer to keep data.
		std::unique_ptr<BufferItem[]> buffer; // 滑动窗口的数组记录数据集
		// Time (in milliseconds) for last item in the time window.
		uint64_t newestItemStartTime{ 0u }; // 当前最新窗口的时间毫秒数
		// Index for the last item in the time window.
		int32_t newestItemIndex{ -1 }; // 最新的窗口的下标
		// Time (in milliseconds) for oldest item in the time window.
		uint64_t oldestItemStartTime{ 0u }; // 总是记录滑动窗口中最小毫秒数
		// Index for the oldest item in the time window.
		int32_t oldestItemIndex{ -1 }; // 最老的下标
		// Total count in the time window.
		size_t totalCount{ 0u }; // 数据同和当前
		// Total bytes transmitted.
		size_t bytes{ 0u };
		// Last value calculated by GetRate().
		uint32_t lastRate{ 0u };
		// Last time GetRate() was called.
		uint64_t lastTime{ 0u };
	};

	class RtpDataCounter
	{
	public:
		explicit RtpDataCounter(size_t windowSizeMs = 2500) : rate(windowSizeMs)
		{
		}

	public:
		void Update(RTC::RtpPacket* packet);
		uint32_t GetBitrate(uint64_t nowMs)
		{
			return this->rate.GetRate(nowMs);
		}
		size_t GetPacketCount() const
		{
			return this->packets;
		}
		size_t GetBytes() const
		{
			return this->rate.GetBytes();
		}

	private:
		RateCalculator rate;
		size_t packets{ 0u };
	};
} // namespace RTC

#endif
