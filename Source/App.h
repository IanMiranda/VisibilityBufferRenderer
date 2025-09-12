#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vk_mem_alloc.h>

#include "Common.h"
#include "Camera.h"

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
		void Render();

	private:
		void InitWindow();
		void InitInstance();
		void InitSurface();
		void InitDevice();
		void InitSwapchain();
		void InitPipeline();
		void InitCommandPool();
		void InitCommandBuffers();
		void InitMsaaTarget();
		void InitDepthBuffer();
		void InitDescriptorPool();
		void InitSyncPrimitives();
		void InitImgui();
		void InitModel();
		void InitVertexBuffer();
		void InitIndexBuffer();
		void InitUniformBuffers();
		void InitTexture();
		void InitDescriptorSets();

		void CleanupSwapchain();
		void RecreateSwapchain();

		VkSurfaceFormatKHR ChooseSurfaceFormat();
		VkPresentModeKHR ChoosePresentMode();
		VkExtent2D ChooseSurfaceExtent();

		VkShaderModule CreateShader(const std::vector<char>& source);

		std::pair<VkBuffer, VmaAllocation> CreateBuffer(VkBufferUsageFlags usage, VkDeviceSize size, VmaAllocationCreateFlags vmaFlags);

		void TransitionSwapchainImage(
			uint32_t imageIndex,
			VkImageLayout oldLayout,
			VkImageLayout newLayout,
			VkAccessFlags2 srcAccess,
			VkAccessFlags2 dstAccess,
			VkPipelineStageFlags2 srcStage,
			VkPipelineStageFlags2 dstStage);

		VkFormat GetSupportedFormat(const std::initializer_list<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags flags);

		VkCommandBuffer CreateImmediateCommandBuffer();
		void CopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
		void SubmitImmediateCommandBuffer(VkCommandBuffer commandBuffer);

		void GenerateMipmaps(VkImage image, VkFormat format, int width, int height, uint32_t levelCount);

		VkPipelineRenderingCreateInfo GetRenderingInfo() const;

	private:
		static constexpr int MaxFramesInFlight = 2;

		struct MatrixData
		{
			glm::mat4 mv;
			glm::mat4 mvp;
			glm::mat4 normal;
		};

		struct LightingData
		{
			glm::vec3 lightPosition;
			float _pad0;
		};

		GLFWwindow* mWindow;

		VkInstance mInstance{ VK_NULL_HANDLE };
		VkDebugUtilsMessengerEXT mDebugMessenger{ VK_NULL_HANDLE };
		VkSurfaceKHR mSurface{ VK_NULL_HANDLE };
		VkPhysicalDevice mGpu{ VK_NULL_HANDLE };
		VkDevice mDevice{ VK_NULL_HANDLE };
		VmaAllocator mAllocator{ VK_NULL_HANDLE };
		VkSwapchainKHR mSwapchain{ VK_NULL_HANDLE };
		std::vector<VkImage> mSwapchainImages;
		std::vector<VkImageView> mSwapchainImageViews;
		VkDescriptorSetLayout mGlobalLayout{ VK_NULL_HANDLE };
		VkDescriptorSetLayout mPerObjectLayout{ VK_NULL_HANDLE };
		VkPipelineLayout mPipeLayout{ VK_NULL_HANDLE };
		VkPipeline mPipe{ VK_NULL_HANDLE };
		VkCommandPool mCommandPool{ VK_NULL_HANDLE };
		VkCommandPool mTransientPool{ VK_NULL_HANDLE };
		VkDescriptorPool mGlobalPool{ VK_NULL_HANDLE };
		VkDescriptorPool mPerObjectPool{ VK_NULL_HANDLE };
		std::vector<VkDescriptorSet> mGlobalSets;
		VkDescriptorSet mPerObjectSet{ VK_NULL_HANDLE };
		VkSampleCountFlagBits mMsaaSamples;

		VkImage mMsaaImage{ VK_NULL_HANDLE };
		VmaAllocation mMsaaAllocation{ VK_NULL_HANDLE };
		VkImageView mMsaaView{ VK_NULL_HANDLE };
		VkFormat mMsaaFormat;

		VkImage mDepthImage{ VK_NULL_HANDLE };
		VmaAllocation mDepthAllocation{ VK_NULL_HANDLE };
		VkImageView mDepthView{ VK_NULL_HANDLE };
		VkFormat mDepthFormat;

		std::vector<VkBuffer> mUniformBuffers;
		std::vector<VmaAllocation> mUniformBufferAllocations;

		std::vector<Vertex> mVertices;
		VkBuffer mVertexBuffer{ VK_NULL_HANDLE };
		VmaAllocation mVertexBufferAllocation{ VK_NULL_HANDLE };
		std::vector<uint32_t> mIndices;
		VkBuffer mIndexBuffer{ VK_NULL_HANDLE };
		VmaAllocation mIndexBufferAllocation{ VK_NULL_HANDLE };

		uint32_t mMipLevelCount;
		VkImage mTexture{ VK_NULL_HANDLE };
		VmaAllocation mTextureAllocation{ VK_NULL_HANDLE };
		VkImageView mTextureView{ VK_NULL_HANDLE };
		VkSampler mTextureSampler{ VK_NULL_HANDLE };

		std::vector<VkCommandBuffer> mCommandBuffers;
		std::vector<VkSemaphore> mAcquireSemaphores;
		std::vector<VkSemaphore> mRenderSemaphores;
		std::vector<VkFence> mRenderFences;
		uint32_t mFrameIndex{ 0 };
		uint32_t mSemaphoreIndex{ 0 };
		bool mFramebufferResized{ false };

		VkQueue mGraphicsQueue{ VK_NULL_HANDLE };
		VkQueue mPresentQueue{ VK_NULL_HANDLE };

		uint32_t mGraphicsIndex;
		uint32_t mPresentIndex;

		VkFormat mSwapchainFormat;
		VkExtent2D mSwapchainExtent;

		Camera mCamera;

	private:
		static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

		static bool InstanceExtensionSupported(const char* name);
		static bool DeviceExtensionSupported(VkPhysicalDevice gpu, const char* name);

		static VkDebugUtilsMessengerCreateInfoEXT GetDebugInfo();

		static std::vector<char> ReadFile(const std::filesystem::path& path);

		static VkPipelineShaderStageCreateInfo MakeShaderStage(VkShaderModule shader, VkShaderStageFlagBits stage, const char* entrypoint);

		static bool HasStencilComponent(VkFormat format);

		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT type,
			const VkDebugUtilsMessengerCallbackDataEXT* data,
			void* userData);
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