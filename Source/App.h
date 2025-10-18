#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vk_mem_alloc.h>

#include "Common.h"
#include "Camera.h"
#include "Device.h"
#include "CommandPool.h"
#include "CommandBuffer.h"
#include "Buffer.h"
#include "Texture2D.h"
#include "TextureCube.h"
#include "DescriptorSetLayout.h"
#include "PipelineLayout.h"
#include "GraphicsPipeline.h"
#include "BindlessSet.h"
#include "ShadowPass.h"

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
		void DrawScene(CommandBuffer& commandBuffer);
		void DrawShadowMap(CommandBuffer& commandBuffer, const glm::mat4& lightView, const glm::mat4& lightProj);

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

		void CleanupSwapchain();
		void RecreateSwapchain();

		void TransitionSwapchainImage(
			VkImage image,
			VkImageLayout oldLayout,
			VkImageLayout newLayout,
			VkAccessFlags2 srcAccess,
			VkAccessFlags2 dstAccess,
			VkPipelineStageFlags2 srcStage,
			VkPipelineStageFlags2 dstStage);

		std::unique_ptr<CommandBuffer> CreateImmediateCommandBuffer();

		void RunImmediateCommands(const std::function<void(CommandBuffer&)>& cmds);

		std::unique_ptr<Texture2D> CreateAndStageTexture(
			const std::filesystem::path& path,
			VkFormat format,
			bool generateMipmaps);

	private:
		static constexpr int MaxFramesInFlight = 2;

		GLFWwindow* mWindow;

		std::unique_ptr<Device> mDevice;
		std::unique_ptr<BindlessSet> mBindlessSet;

		std::unique_ptr<CommandPool> mCommandPool;
		std::unique_ptr<CommandPool> mTransientPool;

		std::unique_ptr<DescriptorSetLayout> mGlobalLayout;
		std::unique_ptr<PipelineLayout> mPipeLayout;
		std::unique_ptr<GraphicsPipeline> mPipe;
		
		VkDescriptorPool mGlobalPool;
		std::vector<VkDescriptorSet> mGlobalSets;

		std::unique_ptr<Texture2D> mDepthImage;

		std::vector<std::unique_ptr<Buffer>> mUniformBuffers;

		std::unique_ptr<Texture2D> mTexture;
		std::unique_ptr<Texture2D> mNormalMap;
		VkSampler mTextureSampler{ VK_NULL_HANDLE };

		std::unique_ptr<TextureCube> mEnvMap;
		VkSampler mEnvMapSampler{ VK_NULL_HANDLE };
		std::unique_ptr<DescriptorSetLayout> mEnvMapSetLayout;
		std::unique_ptr<PipelineLayout> mEnvMapPipeLayout;
		std::unique_ptr<GraphicsPipeline> mEnvMapPipe;
		VkDescriptorPool mEnvMapPool{ VK_NULL_HANDLE };
		VkDescriptorSet mEnvMapSet{ VK_NULL_HANDLE };

		std::unique_ptr<ShadowPass> mShadowPass;

		std::vector<std::unique_ptr<CommandBuffer>> mCommandBuffers;
		std::vector<VkSemaphore> mAcquireSemaphores;
		std::vector<VkSemaphore> mRenderSemaphores;
		std::vector<VkFence> mRenderFences;
		uint32_t mFrameIndex{ 0 };
		uint32_t mSemaphoreIndex{ 0 };
		bool mFramebufferResized{ false };

		Camera mCamera;
		std::vector<Mesh> mMeshes;

	private:
		static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
		static void MousePositionCallback(GLFWwindow* window, double xpos, double ypos);
	};
}
