#include "QueryPool.h"

#include "Device.h"

namespace im
{
	QueryPool::QueryPool(Device& device, VkQueryType type, uint32_t count, VkQueryPipelineStatisticFlags flags)
		: mDevice(device)
	{
		VkQueryPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		poolInfo.queryType = type;
		poolInfo.queryCount = count;
		poolInfo.pipelineStatistics = flags;
		VK_CHECK(vkCreateQueryPool(mDevice.Get(), &poolInfo, nullptr, &mPool));
	}

	QueryPool::~QueryPool()
	{
		mDevice.WaitIdle();
		vkDestroyQueryPool(mDevice.Get(), mPool, nullptr);
	}

	void QueryPool::Reset(uint32_t first, uint32_t count)
	{
		vkResetQueryPool(mDevice.Get(), mPool, first, count);
	}

	void QueryPool::GetResults(uint32_t first, uint32_t count, size_t size, void* data, VkDeviceSize stride, VkQueryResultFlags flags)
	{
		VK_CHECK(vkGetQueryPoolResults(mDevice.Get(), mPool, first, count, size, data, stride, flags));
	}
}