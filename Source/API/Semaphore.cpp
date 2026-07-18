#include "Semaphore.h"

#include "Device.h"

namespace im
{
    Semaphore::Semaphore(Device& device, SemaphoreType type) : mDevice(device)
    {
        VkSemaphoreTypeCreateInfo typeInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        typeInfo.initialValue = 0;
        
        switch (type)
        {
        case SemaphoreType::Binary:
            typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_BINARY;
            break;
        case SemaphoreType::Timeline:
            typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            break;
        }

        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        semInfo.pNext = &typeInfo;

        VK_CHECK(vkCreateSemaphore(mDevice.Get(), &semInfo, nullptr, &mSem));
    }

    Semaphore::~Semaphore()
    {
        mDevice.WaitIdle();
        vkDestroySemaphore(mDevice.Get(), mSem, nullptr);
    }

    void Semaphore::WaitForTime(uint64_t value)
    {
        VkSemaphoreWaitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &mSem;
        waitInfo.pValues = &value;

        VK_CHECK(vkWaitSemaphores(mDevice.Get(), &waitInfo, UINT64_MAX));
    }
}
