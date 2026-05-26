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

		Device& GetDevice() { return mDevice; }
		DescriptorSetAllocator& GetDescriptorSetAllocator() { return mSetAllocator; }

	private:
		bool Begin();
		void End();

		void BeginScene(Scene& scene);

	private:
		void RecreateSwapchain();

	private:
		void InitDepthBuffer();
		void InitPipeline();
		void InitCommandBuffers();
		void InitSyncPrimitives();
		void InitUniformBuffers();
		void InitPbr();
		void InitDescriptors();
		void InitImGui();

		TextureCube EquirectangularToCubemap(ImageView& eqMap);
		TextureCube CalculateDiffuseIrradiance(ImageView& cubeMap);
		TextureCube PrefilterEnvMap(ImageView& cubeMap);
		Texture2D GenerateBrdfLut();

		Buffer CreateCubeVertexBuffer();

	private:
		static constexpr int MaxFramesInFlight = 2;
		static constexpr uint32_t MaxDrawCalls = 100'000;

		struct DrawCall
		{
			uint32_t indexCount;
			uint32_t instanceCount;
			uint32_t firstVertex;
			uint32_t vertexOffset;
			uint32_t firstInstance;
		};

		Window& mWindow;

		Device mDevice;
		BindlessSet mBindlessSet;
		DescriptorSetAllocator mSetAllocator;
		CommandPool mCommandPool;
		std::array<Buffer, 2> mIndirectDrawBuffers;
		std::array<Buffer, 2> mObjectDataBuffers;

		Texture2D mDepthImage;

		std::unique_ptr<DescriptorSetLayout> mMainLayout;
		std::unique_ptr<PipelineLayout> mMainPipeLayout;
		std::unique_ptr<GraphicsPipeline> mMainPipe;

		std::vector<std::unique_ptr<DescriptorSet>> mMainDescSets;

		std::vector<std::unique_ptr<Buffer>> mMainPassBuffers;
		std::vector<std::unique_ptr<Buffer>> mLightBuffers;

		Texture2D mEquirectangularMap;

		TextureCube mEnvMap;
		std::unique_ptr<DescriptorSetLayout> mEnvMapSetLayout;
		std::unique_ptr<PipelineLayout> mEnvMapPipeLayout;
		std::unique_ptr<GraphicsPipeline> mEnvMapPipe;
		std::unique_ptr<DescriptorSet> mEnvMapSet;

		TextureCube mIrradianceMap;
		TextureCube mPrefilteredEnvMap;
		Texture2D mBrdfLut;

		std::vector<std::unique_ptr<CommandBuffer>> mCommandBuffers;
		std::vector<std::unique_ptr<Semaphore>> mAcquireSemaphores;
		std::vector<std::unique_ptr<Semaphore>> mRenderSemaphores;
		std::vector<std::unique_ptr<Fence>> mRenderFences;
		uint32_t mFrameIndex{ 0 };
		uint32_t mSemaphoreIndex{ 0 };
		bool mFramebufferResized{ false };

		Camera mCamera;
		DrawCall* mDrawCallPtr{ nullptr };
		ObjectData* mObjectDataPtr{ nullptr };
		uint32_t mDrawCallCount{ 0 };
		uint32_t mInstanceIndex{ 0 };
	};
}