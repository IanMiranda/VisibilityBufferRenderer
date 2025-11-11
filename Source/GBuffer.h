#pragma once

#include "API/Device.h"
#include "API/Texture2D.h"
#include "API/CommandBuffer.h"

namespace im
{
    class GBuffer
    {
    public:
        GBuffer(Device& device);
        ~GBuffer();

        void Begin(CommandBuffer& cmd);
        void End(CommandBuffer& cmd);

    private:
        Texture2D CreateAttachment(VkFormat format, VkImageUsageFlags usage);

    private:
        Device& mDevice;

        Texture2D mPositionBuffer;
        Texture2D mNormalBuffer;
        Texture2D mAlbedoBuffer;
        Texture2D mSpecularBuffer;
        Texture2D mDepthBuffer;
    };
}