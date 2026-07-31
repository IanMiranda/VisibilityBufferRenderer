#include "Buffer.h"

#include "Device.h"

namespace im
{
	BufferDesc::BufferDesc(
		VkDeviceSize size,
		VkBufferUsageFlags2 usage,
		VmaAllocationCreateFlags allocationFlags)
	: size(size), usage(usage), allocationFlags(allocationFlags)
    {
    }

    BufferDesc BufferDesc::Upload(
		VkDeviceSize size,
		VkBufferUsageFlags2 flags,
		VmaAllocationCreateFlags allocationFlags)
    {
		return BufferDesc(size, flags, allocationFlags);
    }

    Buffer::Buffer(Device& device, const BufferDesc& desc)
		: mDevice(device), mSize(desc.size)
	{
		VkBufferUsageFlags2CreateInfo bufferFlags{ VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO };
		bufferFlags.usage = desc.usage;

		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.pNext = &bufferFlags;
		bufferInfo.size = mSize;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = desc.allocationFlags;

		VK_CHECK(vmaCreateBuffer(mDevice.GetAllocator(), &bufferInfo, &allocInfo, &mBuffer, &mAllocation, nullptr));
	}

	Buffer::Buffer(Device& device, const BufferDesc& desc, const void* data)
		: Buffer(device, desc)
	{
		SetData(data, desc.size);
	}

	Buffer::~Buffer()
	{
		mDevice.WaitIdle();
		vmaDestroyBuffer(mDevice.GetAllocator(), mBuffer, mAllocation);
	}

	VkDeviceAddress Buffer::GetAddress()
	{
		VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addressInfo.buffer = mBuffer;
		return vkGetBufferDeviceAddress(mDevice.Get(), &addressInfo);
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