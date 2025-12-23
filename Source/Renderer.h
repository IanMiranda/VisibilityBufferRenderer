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

		std::unique_ptr<TextureCube> EquirectangularToCubemap(Texture2D& eqMap);
		std::unique_ptr<TextureCube> CalculateDiffuseIrradiance(TextureCube& cubeMap);
		std::unique_ptr<TextureCube> PrefilterEnvMap(TextureCube& cubeMap);
		std::unique_ptr<Texture2D> GenerateBrdfLut();

		Buffer CreateCubeVertexBuffer();

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

		std::unique_ptr<TextureCube> mEnvMap;
		std::unique_ptr<DescriptorSetLayout> mEnvMapSetLayout;
		std::unique_ptr<PipelineLayout> mEnvMapPipeLayout;
		std::unique_ptr<GraphicsPipeline> mEnvMapPipe;
		std::unique_ptr<DescriptorSet> mEnvMapSet;

		std::unique_ptr<TextureCube> mIrradianceMap;
		std::unique_ptr<TextureCube> mPrefilteredEnvMap;
		std::unique_ptr<Texture2D> mBrdfLut;

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