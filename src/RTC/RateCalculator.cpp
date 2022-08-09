#define MS_CLASS "RTC::RateCalculator"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/RateCalculator.hpp"
#include "Logger.hpp"
#include <cmath> // std::trunc()

namespace RTC
{
	void RateCalculator::Update(size_t size, uint64_t nowMs)
	{
		MS_TRACE();

		// Ignore too old data. Should never happen.
		if (nowMs < this->oldestItemStartTime)
		{
			return;
		}

		// Increase bytes.
		this->bytes += size;

		RemoveOldData(nowMs);

		// If the elapsed time from the newest item start time is greater than the
		// item size (in milliseconds), increase the item index.
		if (this->newestItemIndex < 0 || nowMs - this->newestItemStartTime >= this->itemSizeMs)
		{
			this->newestItemIndex++;
			this->newestItemStartTime = nowMs;
			if (this->newestItemIndex >= this->windowItems)
			{
				this->newestItemIndex = 0;
			}

			// Newest index overlaps with the oldest one, remove it.
			if (this->newestItemIndex == this->oldestItemIndex && this->oldestItemIndex != -1)
			{
				MS_WARN_TAG(
				  info,
				  "calculation buffer full, windowSizeMs:%zu ms windowItems:%" PRIu16,
				  this->windowSizeMs,
				  this->windowItems);

				BufferItem& oldestItem = buffer[this->oldestItemIndex];
				this->totalCount -= oldestItem.count;
				oldestItem.count = 0u;
				oldestItem.time  = 0u;
				if (++this->oldestItemIndex >= this->windowItems)
				{
					this->oldestItemIndex = 0;
				}
			}

			// Set the newest item.
			BufferItem& item = buffer[this->newestItemIndex];
			item.count       = size;
			item.time        = nowMs;
		}
		else
		{
			// Update the newest item.
			BufferItem& item = buffer[this->newestItemIndex];
			item.count += size;
		}

		// Set the oldest item index and time, if not set.
		if (this->oldestItemIndex < 0)
		{
			this->oldestItemIndex     = this->newestItemIndex;
			this->oldestItemStartTime = nowMs;
		}

		this->totalCount += size;

		// Reset lastRate and lastTime so GetRate() will calculate rate again even
		// if called with same now in the same loop iteration.
		this->lastRate = 0;
		this->lastTime = 0;
	}

	uint32_t RateCalculator::GetRate(uint64_t nowMs)
	{
		MS_TRACE();

		if (nowMs == this->lastTime)
		{
			return this->lastRate;
		}

		RemoveOldData(nowMs);
		// 比特流 / 总的滑动窗口毫秒数
		float scale = this->scale / this->windowSizeMs;

		this->lastTime = nowMs;
		// 具体就是滑动窗口比特流的公式 =  总滑动窗口中有的数据的总和 * 8000 / windowsizems  ;
		// 原来我以为有什么特殊公式呢！！！
		this->lastRate = static_cast<uint32_t>(std::trunc(this->totalCount * scale + 0.5f));
		//trunc()函数是cmath标头的库函数，用于将值四舍五入(截断)为零，它接受一个数字并返回其大小不大于给定数字的最近整数值。
		return this->lastRate;
	}

	inline void RateCalculator::RemoveOldData(uint64_t nowMs)
	{
		MS_TRACE();

		// No item set.
		if (this->newestItemIndex < 0 || this->oldestItemIndex < 0)
		{
			return;
		}
		// 1. 这个值是根据 当前毫秒数减去全部的滑动窗口时间来计算的
		uint64_t newoldestTime = nowMs - this->windowSizeMs;

		// 2. 判断是否小于滑动窗口中最小的时间毫秒数 ， 如果小于就不需要处理哈（滑动窗口中没有计算该值哈）
		// Oldest item already removed.
		if (newoldestTime <= this->oldestItemStartTime)
		{
			return;
		}
		// 说明上次发送数据时间长太长， 超出滑动窗口的时间长 需要重置滑动窗口
		// A whole window size time has elapsed since last entry. Reset the buffer.
		if (newoldestTime > this->newestItemStartTime)
		{
			Reset();

			return;
		}
		// 移除超出滑动窗口时间的数据 count
		while (this->oldestItemStartTime < newoldestTime)
		{
			BufferItem& oldestItem = buffer[this->oldestItemIndex];
			this->totalCount -= oldestItem.count;
			oldestItem.count = 0u;
			oldestItem.time  = 0u;

			if (++this->oldestItemIndex >= this->windowItems)
			{
				this->oldestItemIndex = 0;
			}

			const BufferItem& newOldestItem = buffer[this->oldestItemIndex];
			// 总是记录滑动窗口中最小毫秒数
			this->oldestItemStartTime       = newOldestItem.time;
		}
	}

	void RtpDataCounter::Update(RTC::RtpPacket* packet)
	{
		uint64_t nowMs = DepLibUV::GetTimeMs();

		this->packets++;
		this->rate.Update(packet->GetSize(), nowMs);
	}
} // namespace RTC
