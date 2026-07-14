#pragma once

#include "Common.h"
#include "Window.h"
#include "API/Device.h"
#include "API/CommandPool.h"
#include "API/CommandBuffer.h"
#include "API/Fence.h"
#include "API/Semaphore.h"
#include "API/DescriptorSetLayout.h"
#include "API/PipelineLayout.h"
#include "API/GraphicsPipeline.h"
#include "API/DescriptorPool.h"
#include "API/Buffer.h"
#include "API/Image.h"
#include "API/ImageView.h"
#include "BindlessSet.h"
#include "DescriptorSetAllocator.h"
#include "Light.h"
#include "Camera.h"
#include "DrawIndirectBackend.h"

namespace im
{
	class Mesh;
	class Scene;

	class Renderer
	{
	private:
		friend class App;

	public:
		Renderer(Window& window);
		~Renderer();

		void Render(Scene& scene);

		void DrawBatch(const std::vector<Object>& batch);

		Window& GetWindow() { return mWindow; }
		Device& GetDevice() { return mDevice; }

	private:
		bool Begin();
		void End();

		void BeginScene(Scene& scene);

	private:
		void RecreateSwapchain();

	private:
		Texture2D InitDepthBuffer();
		void InitCommandBuffers();
		void InitSyncPrimitives();
		void InitImGui();

	private:
		static constexpr int MaxFramesInFlight = 2;

		Window& mWindow;

		Device mDevice;
		CommandPool mCommandPool;

		Texture2D mDepthImage;

		std::vector<std::unique_ptr<CommandBuffer>> mCommandBuffers;
		std::vector<std::unique_ptr<Semaphore>> mAcquireSemaphores;
		std::vector<std::unique_ptr<Semaphore>> mRenderSemaphores;
		std::vector<std::unique_ptr<Fence>> mRenderFences;
		uint32_t mFrameIndex{ 0 };
		uint32_t mSemaphoreIndex{ 0 };
		bool mFramebufferResized{ false };

		DrawIndirectBackend mBackend;
	};
}