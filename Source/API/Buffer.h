#pragma once

#include "Common.h"

namespace im
{
	class Device;

	class Buffer
	{
	public:
		Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags2 usage, VmaAllocationCreateFlags allocationFlags);
		Buffer(Device& device, VkDeviceSize size, const void* data, VkBufferUsageFlags2 usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		~Buffer();

		Buffer(const Buffer& other) = delete;
		Buffer& operator=(const Buffer& other) = delete;

		VkBuffer Get() const { return mBuffer; }
		VkDeviceSize GetSize() const { return mSize; }
		VkDeviceAddress GetAddress();

		void* Map();
		void Unmap();
		void SetData(const void* data, size_t dataSize);

		template <typename T>
		void SetData(const T& data)
		{
			SetData(&data, sizeof(data));
		}

	private:
		Device& mDevice;

		VkBuffer mBuffer;
		VmaAllocation mAllocation;
		VkDeviceSize mSize;
	};
}