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
		void InitModel();
		void InitVertexBuffer();
		void InitIndexBuffer();
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

	private:
		static constexpr int MaxFramesInFlight = 2;

		struct MatrixData
		{
			glm::mat4 mv;
			glm::mat4 mvp;
			glm::mat4 normal;
		};

		GLFWwindow* mWindow;

		VkInstance mInstance;
		VkDebugUtilsMessengerEXT mDebugMessenger;
		VkSurfaceKHR mSurface;
		VkPhysicalDevice mGpu;
		VkDevice mDevice;
		VmaAllocator mAllocator;
		VkSwapchainKHR mSwapchain;
		std::vector<VkImage> mSwapchainImages;
		std::vector<VkImageView> mSwapchainImageViews;
		VkDescriptorSetLayout mSetLayout;
		VkPipelineLayout mPipeLayout;
		VkPipeline mPipe;
		VkCommandPool mCommandPool;
		VkCommandPool mTransientPool;
		VkDescriptorPool mDescPool;
		VkDescriptorSet mDescSet;
		VkSampleCountFlagBits mMsaaSamples;

		VkImage mMsaaImage;
		VmaAllocation mMsaaAllocation;
		VkImageView mMsaaView;
		VkFormat mMsaaFormat;

		VkImage mDepthImage;
		VmaAllocation mDepthAllocation;
		VkImageView mDepthView;
		VkFormat mDepthFormat;

		std::vector<Vertex> mVertices;
		VkBuffer mVertexBuffer;
		VmaAllocation mVertexBufferAllocation;
		std::vector<uint32_t> mIndices;
		VkBuffer mIndexBuffer;
		VmaAllocation mIndexBufferAllocation;

		uint32_t mMipLevelCount;
		VkImage mTexture;
		VmaAllocation mTextureAllocation;
		VkImageView mTextureView;
		VkSampler mTextureSampler;

		std::vector<VkCommandBuffer> mCommandBuffers;
		std::vector<VkSemaphore> mAcquireSemaphores;
		std::vector<VkSemaphore> mRenderSemaphores;
		std::vector<VkFence> mRenderFences;
		uint32_t mFrameIndex{ 0 };
		uint32_t mSemaphoreIndex{ 0 };
		bool mFramebufferResized{ false };

		VkQueue mGraphicsQueue;
		VkQueue mPresentQueue;

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