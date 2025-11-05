#include "Semaphore.h"

#include "Device.h"

namespace im
{
    Semaphore::Semaphore(Device& device) : mDevice(device)
    {
        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(mDevice.Get(), &semInfo, nullptr, &mSem));
    }

    Semaphore::~Semaphore()
    {
        mDevice.WaitIdle();
        vkDestroySemaphore(mDevice.Get(), mSem, nullptr);
    }
}
