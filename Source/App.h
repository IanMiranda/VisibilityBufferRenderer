#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vk_mem_alloc.h>

#include "Common.h"
#include "Camera.h"
#include "Device.h"
#include "Buffer.h"
#include "Texture2D.h"
#include "TextureCube.h"
#include "BindlessSet.h"

namespace im
{
	struct Vertex
	{
		glm::vec3 position;
		glm::vec4 color;
		glm::vec2 uv;
		glm::vec3 normal;

		bool operator==(const Vertex& other) const
		{
			return position == other.position && color == other.color && uv == other.uv && normal == other.normal;
		}
	};

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
		void DrawShadowMap(VkCommandBuffer commandBuffer, const glm::mat4& lightView, const glm::mat4& lightProj);

	private:
		void InitWindow();
		void InitCommandPool();
		void InitDepthBuffer();
		void InitPipeline();
		void InitCommandBuffers();
		void InitDescriptorPool();
		void InitSyncPrimitives();
		void InitImGui();
		void InitModel();
		void InitVertexBuffer();
		void InitIndexBuffer();
		void InitUniformBuffers();
		void InitTexture();
		void InitCubemap();
		void InitShadowResources();
		void InitDescriptorSets();

		void CleanupSwapchain();
		void RecreateSwapchain();

		VkShaderModule CreateShader(const std::vector<char>& source);

		void TransitionSwapchainImage(
			VkImage image,
			VkImageLayout oldLayout,
			VkImageLayout newLayout,
			VkAccessFlags2 srcAccess,
			VkAccessFlags2 dstAccess,
			VkPipelineStageFlags2 srcStage,
			VkPipelineStageFlags2 dstStage);

		VkCommandBuffer CreateImmediateCommandBuffer();
		void CopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
		void SubmitImmediateCommandBuffer(VkCommandBuffer commandBuffer);

	private:
		static constexpr int MaxFramesInFlight = 2;

		struct MatrixData
		{
			glm::mat4 mv;
			glm::mat4 mvp;
			glm::mat4 normal;
			glm::mat4 mvpLight;
		};

		struct LightingData
		{
			glm::mat4 vInverse;
			glm::vec3 lightDir;
			float _pad0;
		};

		struct CubemapData
		{
			glm::mat4 vpInverse;
		};

		GLFWwindow* mWindow;

		std::unique_ptr<Device> mDevice;
		std::unique_ptr<BindlessSet> mBindlessSet;

		VkCommandPool mCommandPool{ VK_NULL_HANDLE };
		VkCommandPool mTransientPool{ VK_NULL_HANDLE };

		VkDescriptorSetLayout mGlobalLayout{ VK_NULL_HANDLE };
		VkDescriptorSetLayout mPerObjectLayout{ VK_NULL_HANDLE };
		
		VkPipelineLayout mPipeLayout{ VK_NULL_HANDLE };
		VkPipeline mPipe{ VK_NULL_HANDLE };
		
		VkDescriptorPool mGlobalPool{ VK_NULL_HANDLE };
		VkDescriptorPool mPerObjectPool{ VK_NULL_HANDLE };

		std::vector<VkDescriptorSet> mGlobalSets;
		VkDescriptorSet mPerObjectSet{ VK_NULL_HANDLE };

		std::unique_ptr<Texture2D> mDepthImage;

		std::vector<std::unique_ptr<Buffer>> mUniformBuffers;

		std::vector<Vertex> mVertices;
		std::unique_ptr<Buffer> mMeshVBO;
		std::vector<uint32_t> mIndices;
		std::unique_ptr<Buffer> mMeshIBO;

		std::unique_ptr<Buffer> mPlaneVBO;
		std::unique_ptr<Buffer> mPlaneIBO;

		std::unique_ptr<Texture2D> mTexture;
		VkSampler mTextureSampler{ VK_NULL_HANDLE };

		std::unique_ptr<TextureCube> mEnvMap;
		VkSampler mEnvMapSampler{ VK_NULL_HANDLE };
		VkDescriptorSetLayout mEnvMapSetLayout{ VK_NULL_HANDLE };
		VkPipelineLayout mEnvMapPipeLayout{ VK_NULL_HANDLE };
		VkPipeline mEnvMapPipe{ VK_NULL_HANDLE };
		VkDescriptorSet mEnvMapSet{ VK_NULL_HANDLE };

		std::unique_ptr<Texture2D> mShadowMap;
		VkSampler mShadowMapSampler{ VK_NULL_HANDLE };
		VkPipelineLayout mShadowPipeLayout{ VK_NULL_HANDLE };
		VkPipeline mShadowPipe{ VK_NULL_HANDLE };

		std::vector<VkCommandBuffer> mCommandBuffers;
		std::vector<VkSemaphore> mAcquireSemaphores;
		std::vector<VkSemaphore> mRenderSemaphores;
		std::vector<VkFence> mRenderFences;
		uint32_t mFrameIndex{ 0 };
		uint32_t mSemaphoreIndex{ 0 };
		bool mFramebufferResized{ false };

		Camera mCamera;

	private:
		static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
		static void MousePositionCallback(GLFWwindow* window, double xpos, double ypos);

		static VkPipelineShaderStageCreateInfo MakeShaderStage(VkShaderModule shader, VkShaderStageFlagBits stage, const char* entrypoint);
	};
}

namespace std
{
	template<> struct hash<im::Vertex>
	{
		size_t operator()(const im::Vertex& vertex) const
		{
			return ((hash<glm::vec3>()(vertex.position) ^ (hash<glm::vec4>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.uv) << 1);
		}
	};
}