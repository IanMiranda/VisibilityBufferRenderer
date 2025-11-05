#include "Fence.h"

#include "Device.h"

namespace im
{
    Fence::Fence(Device& device, bool signaled) : mDevice(device)
    {
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
        VK_CHECK(vkCreateFence(mDevice.Get(), &fenceInfo, nullptr, &mFence));
    }
    
    Fence::~Fence()
    {
        mDevice.WaitIdle();
        vkDestroyFence(mDevice.Get(), mFence, nullptr);
    }

    void Fence::Wait()
    {
        VK_CHECK(vkWaitForFences(mDevice.Get(), 1, &mFence, VK_TRUE, UINT64_MAX));
    }

    void Fence::Reset()
    {
        VK_CHECK(vkResetFences(mDevice.Get(), 1, &mFence));
    }
}