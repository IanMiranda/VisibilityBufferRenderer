#include "Renderer.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <stb_image.h>

#include "API/CommandBuffer.h"
#include "API/RenderPass.h"
#include "API/Shader.h"
#include "Scene.h"
#include "Utils.h"

namespace im
{
    Renderer::Renderer(Window &window)
        : mWindow(window), mDevice(mWindow.Get()),
          mCommandPool(mDevice, mDevice.GetGraphicsIndex(),
                       VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT),
          mTimelineSemaphore(mDevice, SemaphoreType::Timeline),
          mDepthImage(InitDepthBuffer()),
          mBackend(*this, MaxFramesInFlight, *mDepthImage.image)
    {
        InitSyncPrimitives();
        InitCommandBuffers();
    }

    Renderer::~Renderer()
    {
        mDevice.WaitIdle();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void Renderer::Render(Scene &scene)
    {
        if (!Begin())
            return;

        BeginScene(scene);
        scene.Render();

        End();
    }

    bool Renderer::Begin()
    {
        Swapchain &swapchain = mDevice.GetSwapchain();

        mTimelineSemaphore.WaitForTime(
            mFrames[mFrameIndex].timestampOfCompletion);

        auto [res, imageIndex] =
            swapchain.AcquireNextImage(*mFrames[mFrameIndex].acquireSemaphore);
        if (res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapchain();
            return false;
        }
        else
        {
            VK_CHECK(res);
        }

        // ImGui_ImplVulkan_NewFrame();
        // ImGui_ImplGlfw_NewFrame();
        // ImGui::NewFrame();

        // ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
        //                              ImGuiDockNodeFlags_PassthruCentralNode);

        auto &commandBuffer = *mFrames[mFrameIndex].commandBuffer;
        commandBuffer.Begin();

        return true;
    }

    void Renderer::End()
    {
        auto &commandBuffer = *mFrames[mFrameIndex].commandBuffer;

        mBackend.End(*this, commandBuffer, mFrameIndex);

        commandBuffer.Barrier(
            {},
            {ImageMemoryBarrier(
                mDevice.GetSwapchain()
                    .GetImages()[mDevice.GetSwapchain().GetImageIndex()],
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE,
                VK_ACCESS_2_NONE, VK_IMAGE_ASPECT_COLOR_BIT)},
            {});

        commandBuffer.Barrier(
            {MemoryBarrier(
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT)},
            {}, {});

        // commandBuffer.BeginRendering(
        //     {ColorAttachment(
        //         mDevice.GetSwapchain()
        //             .GetViews()[mDevice.GetSwapchain().GetImageIndex()],
        //         VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE)},
        //     Scissor(mDevice.GetSwapchain().GetExtent()));

        // ImGui::Render();
        // ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
        //                                 commandBuffer.Get());

        // commandBuffer.EndRendering();

        // commandBuffer.Barrier(
        //     {},
        //     {ImageMemoryBarrier(
        //         mDevice.GetSwapchain()
        //             .GetImages()[mDevice.GetSwapchain().GetImageIndex()],
        //         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        //         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        //         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
        //             VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
        //         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE,
        //         VK_ACCESS_2_NONE, VK_IMAGE_ASPECT_COLOR_BIT)},
        //     {});

        commandBuffer.End();

        mDevice.Submit(
            {commandBuffer},
            {{*mFrames[mFrameIndex].acquireSemaphore,
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
            {{mTimelineSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
              mNextTimestampOfCompletion},
             {*mRenderSemaphores[mDevice.GetSwapchain().GetImageIndex()],
              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}});

        // ImGui::UpdatePlatformWindows();
        // ImGui::RenderPlatformWindowsDefault();

        VkResult res = mDevice.GetSwapchain().Present(
            *mRenderSemaphores[mDevice.GetSwapchain().GetImageIndex()]);
        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR ||
            mFramebufferResized)
        {
            mFramebufferResized = false;
            RecreateSwapchain();
        }
        else
        {
            VK_CHECK(res);
        }

        mFrames[mFrameIndex].timestampOfCompletion = mNextTimestampOfCompletion;
        mNextTimestampOfCompletion += 10;
        mFrameIndex = (mFrameIndex + 1) % MaxFramesInFlight;
    }

    void Renderer::BeginScene(Scene &scene)
    {
        mBackend.BeginScene(scene, *mFrames[mFrameIndex].commandBuffer,
                            *mDepthImage.view, mFrameIndex);
    }

    void Renderer::DrawBatch(Scene &scene, const std::vector<VbObject> &batch)
    {
        mBackend.DrawBatch(scene, *mFrames[mFrameIndex].commandBuffer, batch,
                           mFrameIndex);
    }

    void Renderer::RecreateSwapchain()
    {
        mWindow.WaitForNonMinimized();

        mDevice.WaitIdle();
        mDevice.GetSwapchain().Recreate();
        InitDepthBuffer();
    }

    Texture2D Renderer::InitDepthBuffer()
    {
        const auto swapExtent = mDevice.GetSwapchain().GetExtent();
        Texture2D res;
        res.image = std::make_unique<Image>(
            mDevice, mDevice.GetDepthFormat(),
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, swapExtent.width,
            swapExtent.height, 1, 1, VK_IMAGE_TYPE_2D, 1);
        res.view = std::make_unique<ImageView>(
            mDevice, *res.image, VK_IMAGE_VIEW_TYPE_2D,
            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1);

        mDevice.RunImmediateCommands([&res](CommandBuffer &cmds) {
            cmds.Barrier({},
                         {ImageMemoryBarrier(
                             *res.image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                             VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                             VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                             VK_IMAGE_ASPECT_DEPTH_BIT)},
                         {});
        });
        return res;
    }

    void Renderer::InitCommandBuffers()
    {
        auto commandBuffers = mCommandPool.Allocate(MaxFramesInFlight);
        for (int i = 0; i < MaxFramesInFlight; ++i)
        {
            mFrames[i].commandBuffer = std::move(commandBuffers[i]);
        }
    }

    void Renderer::InitSyncPrimitives()
    {
        assert(mRenderSemaphores.empty());

        mRenderSemaphores.reserve(mDevice.GetSwapchain().GetViews().size());

        for (size_t i = 0; i < mDevice.GetSwapchain().GetViews().size(); ++i)
        {
            mRenderSemaphores.emplace_back(
                std::make_unique<Semaphore>(mDevice));
        }

        for (size_t i = 0; i < MaxFramesInFlight; ++i)
        {
            mFrames[i].acquireSemaphore = std::make_unique<Semaphore>(mDevice);
        }
    }

    void Renderer::InitImGui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        ImGui_ImplGlfw_InitForVulkan(mWindow.Get(), true);

        const auto format = mDevice.GetSwapchain().GetFormat();
        VkPipelineRenderingCreateInfo renderingInfo{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &format;
        renderingInfo.depthAttachmentFormat = mDevice.GetDepthFormat();

        ImGui_ImplVulkan_InitInfo imguiVulkanInfo{};
        imguiVulkanInfo.ApiVersion = VK_API_VERSION_1_4;
        imguiVulkanInfo.CheckVkResultFn = [](VkResult err) { VK_CHECK(err); };
        imguiVulkanInfo.DescriptorPoolSize = 128;
        imguiVulkanInfo.Device = mDevice.Get();
        imguiVulkanInfo.ImageCount = mDevice.GetSwapchain().GetViews().size();
        imguiVulkanInfo.MinImageCount = MaxFramesInFlight;
        imguiVulkanInfo.Instance = mDevice.GetInstance();
        imguiVulkanInfo.PhysicalDevice = mDevice.GetGpu();
        imguiVulkanInfo.UseDynamicRendering = true;
        imguiVulkanInfo.Queue = mDevice.GetGraphicsQueue();
        imguiVulkanInfo.QueueFamily = mDevice.GetGraphicsIndex();
        imguiVulkanInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        imguiVulkanInfo.PipelineInfoMain.PipelineRenderingCreateInfo =
            renderingInfo;

        ImGui_ImplVulkan_Init(&imguiVulkanInfo);
    }
} // namespace im