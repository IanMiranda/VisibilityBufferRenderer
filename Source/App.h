#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vk_mem_alloc.h>

#include "Common.h"
#include "Window.h"
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
#include "BindlessSet.h"
#include "Skybox.h"
#include "Renderer.h"
#include "Light.h"

namespace im
{
	class Renderer;

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
		void UpdateLightPositions();

		void DrawScene();
		void DrawUI();

	private:
		void InitWindow();
		void InitMeshes();

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
		Window mWindow;
		Renderer mRenderer;

		Material mMaterial;
		
		Camera mCamera;
		std::vector<Mesh> mMeshes;

		std::vector<PointLight> mPointLights;

	private:
		static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
		static void MousePositionCallback(GLFWwindow* window, double xpos, double ypos);
		static void KeyCallback(GLFWwindow* window, int key, int scanCode, int action, int mods);
	};
}
