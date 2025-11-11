#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vk_mem_alloc.h>

#include "Common.h"
#include "Camera.h"
#include "API/Device.h"
#include "API/CommandPool.h"
#include "API/CommandBuffer.h"
#include "API/Fence.h"
#include "API/Semaphore.h"
#include "API/Buffer.h"
#include "API/Texture2D.h"
#include "API/TextureCube.h"
#include "API/Shader.h"
#include "API/DescriptorSetLayout.h"
#include "API/PipelineLayout.h"
#include "API/GraphicsPipeline.h"
#include "API/DescriptorPool.h"
#include "API/DescriptorSet.h"
#include "API/BindlessSet.h"
#include "ShadowPass.h"
#include "GBuffer.h"
#include "Light.h"

namespace im
{
	class App
	{
	public:
		App();
		~App();

		App(App&& other) noexcept = delete;
		App& operator=(App&& other) noexcept = delete;

		App(const App& other) = delete;
		App& operator=(const App& other) = delete;

		void Run();

	private:
		void Update(float deltaTime);
		void Render();

	private:
		void UpdateLights();

		void DrawScene(CommandBuffer& commandBuffer);
		void DrawSkybox(CommandBuffer& commandBuffer, const glm::mat4& view, const glm::mat4& proj);
		void DrawShadowMap(CommandBuffer& commandBuffer, const glm::mat4& lightView, const glm::mat4& lightProj);
		void DrawUI();

		void GeometryPass(CommandBuffer& commandBuffer, const glm::mat4& view, const glm::mat4& proj);
		void LightingPass(CommandBuffer& commandBuffer, const glm::mat4& view);

	private:
		void InitWindow();
		void InitCommandPool();
		void InitDepthBuffer();
		void InitPipeline();
		void InitCommandBuffers();
		void InitSyncPrimitives();
		void InitImGui();
		void InitMeshes();
		void InitUniformBuffers();
		void InitCubemap();
		void InitShadowResources();
		void InitDescriptors();

		void RecreateSwapchain();

		void RunImmediateCommands(const std::function<void(CommandBuffer&)>& cmds);

		std::unique_ptr<Texture2D> CreateAndStageTexture(
			const std::filesystem::path& path,
			VkFormat format,
			bool generateMipmaps);

		Mesh UploadMesh(
			const std::vector<Vertex>& vertices,
			const std::vector<uint32_t>& indices,
			const Material& material,
			const glm::mat4& transform
		);

	private:
		static constexpr int MaxFramesInFlight = 2;

		GLFWwindow* mWindow;

		std::unique_ptr<Device> mDevice;
		std::unique_ptr<BindlessSet> mBindlessSet;

		std::unique_ptr<CommandPool> mCommandPool;
		std::unique_ptr<CommandPool> mImmediatePool;

		std::unique_ptr<DescriptorSetLayout> mGlobalLayout;
		std::unique_ptr<PipelineLayout> mPipeLayout;
		std::unique_ptr<GraphicsPipeline> mPipe;

		std::unique_ptr<DescriptorSetLayout> mGeomDescLayout;
		std::unique_ptr<PipelineLayout> mGeomPipeLayout;
		std::unique_ptr<GraphicsPipeline> mGeomPipe;
		std::unique_ptr<DescriptorPool> mDeferredDescPool;
		std::vector<std::unique_ptr<DescriptorSet>> mGeomSets;
		std::unique_ptr<DescriptorSetLayout> mLightDescLayout;
		std::unique_ptr<PipelineLayout> mLightPipeLayout;
		std::unique_ptr<GraphicsPipeline> mLightPipe;
		std::vector<std::unique_ptr<DescriptorSet>> mLightSets;
		
		std::unique_ptr<DescriptorPool> mGlobalPool;
		std::vector<std::unique_ptr<DescriptorSet>> mGlobalSets;

		std::unique_ptr<Texture2D> mDepthImage;

		std::vector<std::unique_ptr<Buffer>> mGlobalPassBuffers;
		std::vector<std::unique_ptr<Buffer>> mLightBuffers;
		std::vector<std::unique_ptr<Buffer>> mGeomPassBuffers;
		std::vector<std::unique_ptr<Buffer>> mLightPassBuffers;

		std::shared_ptr<Texture2D> mDiffuseMap;
		std::shared_ptr<Texture2D> mSpecularMap;
		std::shared_ptr<Texture2D> mNormalMap;

		std::unique_ptr<TextureCube> mEnvMap;
		std::unique_ptr<DescriptorSetLayout> mEnvMapSetLayout;
		std::unique_ptr<PipelineLayout> mEnvMapPipeLayout;
		std::unique_ptr<GraphicsPipeline> mEnvMapPipe;
		std::unique_ptr<DescriptorPool> mEnvMapPool;
		std::unique_ptr<DescriptorSet> mEnvMapSet;

		std::unique_ptr<ShadowPass> mShadowPass;

		std::vector<std::unique_ptr<GBuffer>> mGBuffers;

		std::vector<std::unique_ptr<CommandBuffer>> mCommandBuffers;
		std::vector<std::unique_ptr<Semaphore>> mAcquireSemaphores;
		std::vector<std::unique_ptr<Semaphore>> mRenderSemaphores;
		std::vector<std::unique_ptr<Fence>> mRenderFences;
		uint32_t mFrameIndex{ 0 };
		uint32_t mSemaphoreIndex{ 0 };
		bool mFramebufferResized{ false };

		Camera mCamera;
		std::vector<Mesh> mMeshes;

		std::vector<PointLight> mPointLights;

	private:
		static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
		static void MousePositionCallback(GLFWwindow* window, double xpos, double ypos);
		static void KeyCallback(GLFWwindow* window, int key, int scanCode, int action, int mods);
	};
}
