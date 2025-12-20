#include "Buffer.h"

#include "Device.h"

namespace im
{
	Buffer::Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocationFlags)
		: mDevice(device), mSize(size)
	{
		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = mSize;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = allocationFlags;

		VK_CHECK(vmaCreateBuffer(mDevice.GetAllocator(), &bufferInfo, &allocInfo, &mBuffer, &mAllocation, nullptr));
	}

	Buffer::Buffer(Device& device, VkDeviceSize size, const void* data, VkBufferUsageFlags usage)
		: Buffer(device, size, usage, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
	{
		SetData(data, size);
	}

	Buffer::~Buffer()
	{
		mDevice.WaitIdle();
		vmaDestroyBuffer(mDevice.GetAllocator(), mBuffer, mAllocation);
	}

	void* Buffer::Map()
	{
		void* mappedData;
		VK_CHECK(vmaMapMemory(mDevice.GetAllocator(), mAllocation, &mappedData));
		return mappedData;
	}

	void Buffer::Unmap()
	{
		vmaUnmapMemory(mDevice.GetAllocator(), mAllocation);
	}

	void Buffer::SetData(const void* data, size_t dataSize)
	{
		void* const address = Map();
		std::memcpy(address, data, dataSize);
		Unmap();
	}
}