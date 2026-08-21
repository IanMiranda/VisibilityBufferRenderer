#include "GraphicsPipeline.h"

#include "Device.h"
#include "PipelineLayout.h"
#include "Shader.h"

namespace im
{
    VkPipelineInputAssemblyStateCreateInfo InputAssembly(
        VkPrimitiveTopology topology, bool enableRestart)
    {
        VkPipelineInputAssemblyStateCreateInfo res{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        res.topology = topology;
        res.primitiveRestartEnable = enableRestart;
        return res;
    }

    VkPipelineRasterizationStateCreateInfo Rasterizer(VkCullModeFlags cull,
                                                      VkFrontFace frontFace,
                                                      VkPolygonMode polygonMode)
    {
        VkPipelineRasterizationStateCreateInfo res{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        res.cullMode = cull;
        res.frontFace = frontFace;
        res.polygonMode = polygonMode;
        res.lineWidth = 1.0f;
        return res;
    }

    VkPipelineMultisampleStateCreateInfo Multisample(
        VkSampleCountFlagBits samples)
    {
        VkPipelineMultisampleStateCreateInfo res{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        res.rasterizationSamples = samples;
        return res;
    }

    ColorBlendAttachment ColorAttachment(VkFormat format)
    {
        VkPipelineColorBlendAttachmentState colorAttachment{};
        colorAttachment.blendEnable = VK_FALSE;
        colorAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        return {format, colorAttachment};
    }

    DepthStencilAttachment DepthStencil(VkFormat format, VkCompareOp compareOp,
                                        bool depthWrite)
    {
        VkPipelineDepthStencilStateCreateInfo res{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        res.depthTestEnable = true;
        res.depthWriteEnable = depthWrite;
        res.depthCompareOp = compareOp;
        return {format, res};
    }

    GraphicsPipelineDesc::GraphicsPipelineDesc(
        const PipelineLayout &layout, const Shader &shader,
        const std::vector<InputBinding> &bindings,
        const VkPipelineInputAssemblyStateCreateInfo &inputAssembly,
        const VkPipelineRasterizationStateCreateInfo &rasterizer,
        const VkPipelineMultisampleStateCreateInfo &multisample,
        const std::vector<ColorBlendAttachment> &colorAttachments,
        std::optional<DepthStencilAttachment> depthStencil)
        : mLayout(layout), mShader(shader),
          mInputBindings(GetVertexInputBindings(bindings)),
          mInputAttribs(GetVertexInputAttribs(bindings)),
          mInputAssembly(inputAssembly), mRasterizer(rasterizer),
          mMultisample(multisample), mDepthStencil(depthStencil)
    {
        mVertexInput.vertexBindingDescriptionCount = mInputBindings.size();
        mVertexInput.pVertexBindingDescriptions = mInputBindings.data();
        mVertexInput.vertexAttributeDescriptionCount = mInputAttribs.size();
        mVertexInput.pVertexAttributeDescriptions = mInputAttribs.data();

        mColorFormats.reserve(colorAttachments.size());
        mColorBlendStates.reserve(colorAttachments.size());
        for (const auto &[format, attachment] : colorAttachments)
        {
            mColorFormats.emplace_back(format);
            mColorBlendStates.emplace_back(attachment);
        }
    }

    std::vector<VkVertexInputBindingDescription> GraphicsPipelineDesc::
        GetVertexInputBindings(const std::vector<InputBinding> &bindings)
    {
        std::vector<VkVertexInputBindingDescription> inputBindings;
        inputBindings.reserve(bindings.size());
        for (uint32_t i = 0; i < bindings.size(); ++i)
        {
            VkVertexInputBindingDescription res{};
            res.binding = i;
            res.inputRate = bindings[i].inputRate;
            res.stride = bindings[i].stride;
            inputBindings.emplace_back(res);
        };
        return inputBindings;
    }

    std::vector<VkVertexInputAttributeDescription> GraphicsPipelineDesc::
        GetVertexInputAttribs(const std::vector<InputBinding> &bindings)
    {
        std::vector<VkVertexInputAttributeDescription> res;
        for (uint32_t i = 0; i < bindings.size(); ++i)
        {
            for (const auto &attrib : bindings[i].attributes)
            {
                VkVertexInputAttributeDescription inputAttrib{};
                inputAttrib.binding = i;
                inputAttrib.location = attrib.location;
                inputAttrib.format = attrib.format;
                inputAttrib.offset = attrib.offset;
                res.emplace_back(inputAttrib);
            }
        };
        return res;
    }

    GraphicsPipeline::GraphicsPipeline(Device &device,
                                       const GraphicsPipelineDesc &desc)
        : mDevice(device)
    {
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        std::array dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                    VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicState{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = dynamicStates.size();
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRenderingCreateInfo renderingInfo{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        renderingInfo.colorAttachmentCount = desc.mColorFormats.size();
        renderingInfo.pColorAttachmentFormats = desc.mColorFormats.data();
        renderingInfo.depthAttachmentFormat =
            desc.mDepthStencil.has_value() ? desc.mDepthStencil.value().first
                                           : VK_FORMAT_UNDEFINED;

        VkPipelineColorBlendStateCreateInfo colorBlend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.attachmentCount = desc.mColorBlendStates.size();
        colorBlend.pAttachments = desc.mColorBlendStates.data();
        colorBlend.logicOpEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo pipelineInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = desc.mShader.mStages.size();
        pipelineInfo.pStages = desc.mShader.mStages.data();
        pipelineInfo.pVertexInputState = &desc.mVertexInput;
        pipelineInfo.pInputAssemblyState = &desc.mInputAssembly;
        pipelineInfo.pViewportState = &viewport;
        pipelineInfo.pRasterizationState = &desc.mRasterizer;
        pipelineInfo.pMultisampleState = &desc.mMultisample;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        if (desc.mDepthStencil.has_value())
            pipelineInfo.pDepthStencilState =
                &desc.mDepthStencil.value().second;
        pipelineInfo.layout = desc.mLayout.Get();

        VK_CHECK(vkCreateGraphicsPipelines(mDevice.Get(),
                                           mDevice.GetPipelineCache(), 1,
                                           &pipelineInfo, nullptr, &mPipeline));
    }

    GraphicsPipeline::~GraphicsPipeline()
    {
        mDevice.WaitIdle();
        vkDestroyPipeline(mDevice.Get(), mPipeline, nullptr);
    }
} // namespace im