#include "CommandPool.h"

#include "Device.h"

namespace im
{
	CommandPool::CommandPool(Device& device, uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags)
		: mDevice(device),
		mQueueFamilyIndex(queueFamilyIndex)
	{
		VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.queueFamilyIndex = mQueueFamilyIndex;
		poolInfo.flags = flags;

		VK_CHECK(vkCreateCommandPool(mDevice.Get(), &poolInfo, nullptr, &mPool));
	}

	CommandPool::~CommandPool()
	{
		mDevice.WaitIdle();
		vkDestroyCommandPool(mDevice.Get(), mPool, nullptr);
	}

	std::unique_ptr<CommandBuffer> CommandPool::Allocate()
	{
		VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocInfo.commandBufferCount = 1;
		allocInfo.commandPool = mPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		VkCommandBuffer commandBuffer;
		VK_CHECK(vkAllocateCommandBuffers(mDevice.Get(), &allocInfo, &commandBuffer));

		return std::make_unique<CommandBuffer>(*this, commandBuffer);
	}

	std::vector<std::unique_ptr<CommandBuffer>> CommandPool::Allocate(size_t count)
	{
		VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocInfo.commandBufferCount = count;
		allocInfo.commandPool = mPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		std::vector<VkCommandBuffer> commandBuffers(count);
		VK_CHECK(vkAllocateCommandBuffers(mDevice.Get(), &allocInfo, commandBuffers.data()));
		
		std::vector<std::unique_ptr<CommandBuffer>> res;
		res.reserve(count);
		for (const auto& cmdBuf : commandBuffers)
			res.emplace_back(std::make_unique<CommandBuffer>(*this, cmdBuf));
	}

	Device& CommandPool::GetDevice()
	{
		return mDevice;
	}
}