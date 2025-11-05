#pragma once

#include "Common.h"

namespace im
{
    class Device;

    class Semaphore
    {
    public:
        Semaphore(Device& device);
        ~Semaphore();

        Semaphore(const Semaphore& other) = delete;
		Semaphore& operator=(const Semaphore& other) = delete;

        VkSemaphore Get() const { return mSem; }
        
    private:
        Device& mDevice;

        VkSemaphore mSem;
    };
}