#pragma once

#include "Common.h"

namespace im
{
	class Device;
	class CommandBuffer;

	class CommandPool
	{
	public:
		CommandPool(Device& device, uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags);
		~CommandPool();

		CommandPool(const CommandPool& other) = delete;
		CommandPool& operator=(const CommandPool& other) = delete;

		std::unique_ptr<CommandBuffer> Allocate();
		std::vector<std::unique_ptr<CommandBuffer>> Allocate(size_t count);

		VkCommandPool Get() { return mPool; }
		Device& GetDevice();

	private:
		Device& mDevice;
		const uint32_t mQueueFamilyIndex;

		VkCommandPool mPool{ VK_NULL_HANDLE };
	};
}