#pragma once

#include "Common.h"

namespace im
{
    class Device;

    class Fence
    {
    public:
        Fence(Device& device, bool signaled);
        ~Fence();

        Fence(const Fence& other) = delete;
		Fence& operator=(const Fence& other) = delete;

        VkFence Get() const { return mFence; }

        void Wait();
        void Reset();

    private:
        Device& mDevice;
        VkFence mFence;
    };
}