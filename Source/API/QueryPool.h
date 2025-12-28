#pragma once

#include "Common.h"

namespace im
{
	class Device;

	class QueryPool
	{
	public:
		QueryPool(Device& device, VkQueryType type, uint32_t count, VkQueryPipelineStatisticFlags flags = 0);
		~QueryPool();

		void Reset(uint32_t first, uint32_t count);
		void GetResults(uint32_t first, uint32_t count, size_t size, void* data, VkDeviceSize stride, VkQueryResultFlags flags);

		VkQueryPool Get() { return mPool; }

	private:
		Device& mDevice;

		VkQueryPool mPool;
	};
}