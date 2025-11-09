#include "GBuffer.h"

#include "Utils.h"

namespace im
{
    GBuffer::GBuffer(Device& device)
        : mDevice(device)
        , mPositionBuffer(
            CreateAttachment(
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            )
        ), mNormalBuffer(
            CreateAttachment(
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            )
        ), mAlbedoBuffer(
            CreateAttachment(
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            )
        ), mSpecularBuffer(
            CreateAttachment(
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            )
        ), mDepthBuffer(
            CreateAttachment(
                mDevice.GetDepthFormat(),
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            )
        )
    {
    }

    GBuffer::~GBuffer()
    {
    }

    void GBuffer::Begin(CommandBuffer& cmd)
    {
        cmd.BeginRendering(
            {
                utils::ColorAttachment(mPositionBuffer.GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE),
                utils::ColorAttachment(mNormalBuffer.GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE),
                utils::ColorAttachment(mAlbedoBuffer.GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE),
                utils::ColorAttachment(mSpecularBuffer.GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE),
            },
            utils::DepthAttachment(mDepthBuffer.GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE),
            utils::Scissor(mDevice.GetSwapchain().GetExtent())
        );
    }

    Texture2D GBuffer::CreateAttachment(VkFormat format, VkImageUsageFlags usage)
    {
        return Texture2D(
            mDevice, format, usage,
            mDevice.GetSwapchain().GetExtent().width,
            mDevice.GetSwapchain().GetExtent().height,
            false
        );
    }
}