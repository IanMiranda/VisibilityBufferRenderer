#pragma once

#include "Common.h"

namespace im
{
    class Device;

    enum class SemaphoreType
    {
        Binary,
        Timeline,
    };

    class Semaphore
    {
    public:
        Semaphore(Device& device, SemaphoreType type = SemaphoreType::Binary);
        ~Semaphore();

        Semaphore(const Semaphore& other) = delete;
		Semaphore& operator=(const Semaphore& other) = delete;

        VkSemaphore Get() const { return mSem; }

        void WaitForTime(uint64_t value);
        
    private:
        Device& mDevice;

        VkSemaphore mSem;
    };

    struct SemaphoreSubmitInfo
    {
        std::reference_wrapper<Semaphore> semaphore;
        VkPipelineStageFlags2 stageMask;
        uint64_t waitValue;

        SemaphoreSubmitInfo(Semaphore& semaphore, VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_NONE, uint64_t waitValue = 0)
            : semaphore(semaphore), stageMask(stageMask), waitValue(waitValue)
        {
        }
    };
}