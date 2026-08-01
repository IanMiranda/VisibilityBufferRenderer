#pragma once

#include "Common.h"

namespace im
{
	class Device;
	
	struct BufferDesc
	{
		VkDeviceSize size;
		VkBufferUsageFlags2 usage;
		VmaAllocationCreateFlags allocationFlags;

		explicit BufferDesc(
			VkDeviceSize size,
			VkBufferUsageFlags2 usage,
			VmaAllocationCreateFlags allocationFlags = 0
		);

		static BufferDesc Upload(
			VkDeviceSize size,
			VkBufferUsageFlags2 flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		);
	};

	class Buffer
	{
	public:
		Buffer(Device& device, const BufferDesc& desc);
		Buffer(Device& device, const BufferDesc& desc, const void* data);
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