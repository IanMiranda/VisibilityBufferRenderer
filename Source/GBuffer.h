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

        Texture2D& GetPositionBuffer() { return mPositionBuffer; }
        Texture2D& GetNormaBuffer() { return mNormalBuffer; }
        Texture2D& GetAlbedoBuffer() { return mAlbedoBuffer; }
        Texture2D& GetSpecularBuffer() { return mSpecularBuffer; }

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