#pragma once

#include "Common.h"

namespace im
{
	class Device;

	class Buffer
	{
	public:
		Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocationFlags);
		Buffer(Device& device, VkDeviceSize size, const void* data);
		~Buffer();

		Buffer(const Buffer& other) = delete;
		Buffer& operator=(const Buffer& other) = delete;

		VkBuffer Get() const { return mBuffer; }
		VkDeviceSize GetSize() const { return mSize; }

		void* Map();
		void Unmap();
		void SetData(const void* data, size_t dataSize);

	private:
		Device& mDevice;

		VkBuffer mBuffer;
		VmaAllocation mAllocation;
		VkDeviceSize mSize;
	};
}