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
#include "API/Texture2D.h"
#include "BindlessSet.h"
#include "DescriptorSetAllocator.h"
#include "Light.h"
#include "Camera.h"

namespace im
{
	class Mesh;
	class Skybox;

	class Renderer
	{
	private:
		friend class App;

	public:
		Renderer(Window& window);
		~Renderer();

		bool Begin();
		void End();

		void BeginScene(const Camera& camera, std::span<PointLight> pointLights);

		void DrawMesh(const Mesh& mesh);

		Device& GetDevice() { return mDevice; }
		DescriptorSetAllocator& GetDescriptorSetAllocator() { return mSetAllocator; }

	private:
		void RecreateSwapchain();

	private:
		void InitDepthBuffer();
		void InitPipeline();
		void InitCommandBuffers();
		void InitSyncPrimitives();
		void InitUniformBuffers();
		void InitDescriptors();
		void InitImGui();

	private:
		static constexpr int MaxFramesInFlight = 2;

		Window& mWindow;

		Device mDevice;
		BindlessSet mBindlessSet;
		DescriptorSetAllocator mSetAllocator;
		CommandPool mCommandPool;

		std::unique_ptr<Texture2D> mDepthImage;

		std::unique_ptr<DescriptorSetLayout> mMainLayout;
		std::unique_ptr<PipelineLayout> mMainPipeLayout;
		std::unique_ptr<GraphicsPipeline> mMainPipe;

		std::vector<std::unique_ptr<DescriptorSet>> mMainDescSets;

		std::vector<std::unique_ptr<Buffer>> mMainPassBuffers;
		std::vector<std::unique_ptr<Buffer>> mLightBuffers;

		std::unique_ptr<Skybox> mSkybox;

		std::unique_ptr<Texture2D> mEquirectangularMap;
		std::unique_ptr<Buffer> mCubeVertexBuffer;
		std::unique_ptr<DescriptorSetLayout> mCubeDescSetLayout;
		std::unique_ptr<PipelineLayout> mCubePipeLayout;
		std::unique_ptr<GraphicsPipeline> mCubePipe;
		std::unique_ptr<DescriptorSet> mCubeDescSet;

		std::vector<std::unique_ptr<CommandBuffer>> mCommandBuffers;
		std::vector<std::unique_ptr<Semaphore>> mAcquireSemaphores;
		std::vector<std::unique_ptr<Semaphore>> mRenderSemaphores;
		std::vector<std::unique_ptr<Fence>> mRenderFences;
		uint32_t mFrameIndex{ 0 };
		uint32_t mSemaphoreIndex{ 0 };
		bool mFramebufferResized{ false };

		Camera mCamera;
	};
}