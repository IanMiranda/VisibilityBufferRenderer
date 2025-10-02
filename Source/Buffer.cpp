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

	Buffer::Buffer(Device& device, VkDeviceSize size, const void* data)
		: mDevice(device), mSize(size)
	{
		const auto allocator = mDevice.GetAllocator();

		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = mSize;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

		VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &mBuffer, &mAllocation, nullptr));

		void* mappedData = Map();
		std::memcpy(mappedData, data, size);
		Unmap();
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

	void Buffer::CopyInto(VkCommandBuffer commandBuffer, Buffer& other)
	{
		VkBufferCopy copy{};
		copy.size = mSize;
		copy.srcOffset = 0;
		copy.dstOffset = 0;
		vkCmdCopyBuffer(commandBuffer, mBuffer, other.mBuffer, 1, &copy);
	}

	void Buffer::CopyInto(VkCommandBuffer commandBuffer, Buffer& other, VkDeviceSize size)
	{
		VkBufferCopy copy{};
		copy.size = size;
		copy.srcOffset = 0;
		copy.dstOffset = 0;
		vkCmdCopyBuffer(commandBuffer, mBuffer, other.mBuffer, 1, &copy);
	}
}