#pragma once

#include "API/Buffer.h"
#include "API/CommandBuffer.h"
#include "API/CommandPool.h"
#include "API/DescriptorPool.h"
#include "API/DescriptorSetLayout.h"
#include "API/Device.h"
#include "API/Fence.h"
#include "API/GraphicsPipeline.h"
#include "API/Image.h"
#include "API/ImageView.h"
#include "API/PipelineLayout.h"
#include "API/Semaphore.h"
#include "BindlessSet.h"
#include "Camera.h"
#include "Common.h"
#include "DeferredBackend.h"
#include "DescriptorSetAllocator.h"
#include "ForwardBackend.h"
#include "Light.h"
#include "VisibilityBufferBackend.h"
#include "Window.h"

namespace im
{
    class Mesh;
    class Scene;

    class Renderer
    {
    private:
        friend class App;

    public:
        Renderer(Window &window);
        ~Renderer();

        void Render(Scene &scene);

        void DrawBatch(Scene &scene, const std::vector<VbObject> &batch);

        Window &GetWindow()
        {
            return mWindow;
        }
        constexpr Device &GetDevice()
        {
            return mDevice;
        }

    private:
        bool Begin();
        void End(Scene &scene);

        void BeginScene(Scene &scene);

    private:
        void RecreateSwapchain();

    private:
        Texture2D InitDepthBuffer();
        void InitCommandBuffers();
        void InitSyncPrimitives();
        void InitImGui();

    private:
        static constexpr int MaxFramesInFlight = 2;

        struct FrameContext
        {
            std::unique_ptr<CommandBuffer> commandBuffer;
            std::unique_ptr<Semaphore> acquireSemaphore;
            uint64_t timestampOfCompletion{0};
        };

    private:
        Window &mWindow;

        Device mDevice;
        CommandPool mCommandPool;
        Semaphore mTimelineSemaphore;

        Texture2D mDepthImage;

        std::array<FrameContext, MaxFramesInFlight> mFrames;
        std::vector<std::unique_ptr<Semaphore>> mRenderSemaphores;
        uint32_t mFrameIndex{0};
        uint32_t mNextTimestampOfCompletion{10};
        bool mFramebufferResized{false};

        ForwardBackend mFwBackend;
        DeferredBackend mDfBackend;
        VisibilityBufferBackend mVbBackend;

        enum class Backend
        {
            Forward,
            Deferred,
            Visibility,
        } mCurrentBackend{Backend::Forward};
        inline static constexpr std::string_view sBackends[3]{
            "Forward", "Deferred", "Visibility"};
    };
} // namespace im