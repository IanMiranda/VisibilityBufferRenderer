#include "App.h"

#include <array>
#include <optional>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <string_view>

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
// #include <ktx.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_glfw.h>

#include "Utils.h"

#define DEFERRED_SHADING 0

namespace im
{
	static constexpr uint32_t gMaxTextures = 512;
	static constexpr uint32_t gDefaultWindowWidth = 1280;
	static constexpr uint32_t gDefaultWindowHeight = 720;

	App::App()
		: mWindow(gDefaultWindowWidth, gDefaultWindowHeight, "Vulkan App")
		, mDevice(mWindow.Get())
		, mBindlessSet(mDevice, gMaxTextures)
		, mCommandPool(mDevice, mDevice.GetGraphicsIndex(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
	{
		InitWindow();

		InitDepthBuffer();
		InitImGui();
		InitSyncPrimitives();
		InitCommandBuffers();
		InitPipeline();
		InitMeshes();
		InitUniformBuffers();
		// InitShadowResources();

		mGBuffers.reserve(MaxFramesInFlight);
		for (int i = 0; i < MaxFramesInFlight; ++i)
			mGBuffers.emplace_back(std::make_unique<GBuffer>(mDevice));

		mSkybox = std::make_unique<Skybox>(mDevice,
			std::array<std::filesystem::path, Skybox::Faces>{
				"./Assets/Textures/Stadium/px.png",
				"./Assets/Textures/Stadium/nx.png",
				"./Assets/Textures/Stadium/py.png",
				"./Assets/Textures/Stadium/ny.png",
				"./Assets/Textures/Stadium/pz.png",
				"./Assets/Textures/Stadium/nz.png",
			}
		);
		
		InitDescriptors();

		srand(time(nullptr));
		mPointLights.resize(8);
	}

	App::~App()
	{
		mDevice.WaitIdle();

		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void App::Run()
	{
		float lastTime = glfwGetTime();
		float fpsLast = glfwGetTime();
		int frames = 0;
		while (!mWindow.ShouldClose())
		{
			glfwPollEvents();
			const float currentTime = glfwGetTime();
			const float deltaTime = currentTime - lastTime;

			Update(deltaTime);
			Render();
			++frames;
			if (glfwGetTime() - fpsLast >= 1.0)
			{
				fmt::println("FPS: {}", frames);
				fpsLast = glfwGetTime();
				frames = 0;
			}

			lastTime = currentTime;
		}
	}

	void App::Update(float deltaTime)
	{
		if (glfwGetKey(mWindow.Get(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(mWindow.Get(), GLFW_TRUE);

		constexpr float moveFactor = 2.5f;
		const auto front = mCamera.front;
		constexpr glm::vec3 up(0.0f, 1.0f, 0.0f);
		const glm::vec3 right = glm::normalize(glm::cross(front, up));

		if (glfwGetKey(mWindow.Get(), GLFW_KEY_W) == GLFW_PRESS)
			mCamera.position += front * deltaTime * moveFactor;
		else if (glfwGetKey(mWindow.Get(), GLFW_KEY_S) == GLFW_PRESS)
			mCamera.position += -front * deltaTime * moveFactor;

		if (glfwGetKey(mWindow.Get(), GLFW_KEY_A) == GLFW_PRESS)
			mCamera.position += -right * deltaTime * moveFactor;
		else if (glfwGetKey(mWindow.Get(), GLFW_KEY_D) == GLFW_PRESS)
			mCamera.position += right * deltaTime * moveFactor;

		mCamera.Update();

		UpdateLightPositions();
	}

	void App::Render()
	{
		Swapchain& swapchain = mDevice.GetSwapchain();

		mRenderFences[mFrameIndex]->Wait();

		auto [res, imageIndex] = swapchain.AcquireNextImage(*mAcquireSemaphores[mSemaphoreIndex]);
		if (res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			RecreateSwapchain();
			return;
		}
		else
		{
			VK_CHECK(res);
		}

		mRenderFences[mFrameIndex]->Reset();

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		mCommandBuffers[mFrameIndex]->Begin();

		DrawScene(*mCommandBuffers[mFrameIndex]);
		
		mCommandBuffers[mFrameIndex]->End();

		mDevice.Submit(*(mCommandBuffers[mFrameIndex]),
			mAcquireSemaphores[mSemaphoreIndex].get(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			mRenderSemaphores[imageIndex].get(), mRenderFences[mFrameIndex].get());

		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();

		res = swapchain.Present(*mRenderSemaphores[imageIndex]);
		if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || mFramebufferResized)
		{
			mFramebufferResized = false;
			RecreateSwapchain();
		}
		else
		{
			VK_CHECK(res);
		}

		mSemaphoreIndex = (mSemaphoreIndex + 1) % mAcquireSemaphores.size();
		mFrameIndex = (mFrameIndex + 1) % MaxFramesInFlight;
	}

    void App::UpdateLightPositions()
    {
		float offset = 0.0f;
		float distance = 20.0f * sin(glfwGetTime()) + 21.0f;
		for (auto& light : mPointLights)
		{
			light.position = glm::vec3(distance * sin(glfwGetTime() + offset), 1.0f, -distance * cos(glfwGetTime() + offset));
			offset += (2 * 3.14159) / mPointLights.size();
		}
    }

    void App::DrawScene(CommandBuffer& commandBuffer)
    {
		const Swapchain& swapchain = mDevice.GetSwapchain();
		const auto swapExtent = swapchain.GetExtent();

		/* const glm::vec3 lightDir = glm::vec3(2.0f * sin(glfwGetTime()), 2.0f, -2.0f * cos(glfwGetTime()));
		const glm::mat4 lightView = glm::lookAt(
			lightDir,
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));

		// Transform and project to "light space"
		constexpr float shadowProjDim = 10.0f;
		const glm::mat4 lightProj = glm::ortho(-shadowProjDim, shadowProjDim, -shadowProjDim, shadowProjDim, 0.1f, shadowProjDim); // No perspective skew for dir light

		DrawShadowMap(commandBuffer, lightView, lightProj);*/

		commandBuffer.BarrierSwapchainImage(
			swapchain.GetImages()[swapchain.GetImageIndex()],
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

		commandBuffer.SetViewportAndScissor(swapExtent);

		glm::mat4 view = mCamera.GetViewMatrix();
		glm::mat4 proj = glm::perspective(glm::radians(75.0f), static_cast<float>(swapExtent.width) / swapExtent.height, 0.1f, 100.0f);

#if !DEFERRED_SHADING
		commandBuffer.BeginRendering(
			{ utils::ColorAttachment(swapchain.GetViews()[swapchain.GetImageIndex()], VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE) },
			utils::DepthAttachment(mDepthImage->GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE),
			utils::Scissor(swapExtent)
		);

		mSkybox->Draw(commandBuffer, view, proj);

		// Forward pass
		GlobalPassData passData{};
		passData.view = view;
		passData.viewProj = proj * view;
		// passData.viewProjLight = lightProj * lightView;
		passData.viewInverse = glm::inverse(view);
		passData.lightCount = mPointLights.size();
		mGlobalPassBuffers[mFrameIndex]->SetData(passData);

		LightData lightData{};
		for (int i = 0; i < mPointLights.size(); ++i)
			lightData.lights[i] = { glm::vec3(view * glm::vec4(mPointLights[i].position, 1.0f)), 0, mPointLights[i].i };
		mLightBuffers[mFrameIndex]->SetData(lightData);

		commandBuffer.BindGraphicsPipeline(*mPipe);
		commandBuffer.BindGraphicsDescriptorSets(*mPipeLayout, 0, { *(mGlobalSets[mFrameIndex]), mBindlessSet.Get() });

		ObjectData pushConsts{};
		for (const auto& mesh : mMeshes)
		{
			pushConsts.model = mesh.transform;
			pushConsts.albedoMapIndex = mBindlessSet.GetOrCreateId(mesh.material.albedoMap);
			pushConsts.metallicMapIndex = mBindlessSet.GetOrCreateId(mesh.material.metallicMap);
			pushConsts.roughnessMapIndex = mBindlessSet.GetOrCreateId(mesh.material.roughnessMap);
			pushConsts.normalMapIndex = mBindlessSet.GetOrCreateId(mesh.material.normalMap);
			pushConsts.aoMapIndex = mBindlessSet.GetOrCreateId(mesh.material.aoMap);
			pushConsts.emissiveMapIndex = mBindlessSet.GetOrCreateId(mesh.material.emissiveMap);

			commandBuffer.PushConstants(*mPipeLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				pushConsts
			);
			commandBuffer.BindVertexBuffer(*mesh.vertexBuffer);
			commandBuffer.BindIndexBuffer(*mesh.indexBuffer);
			commandBuffer.DrawIndexed(mesh.indexCount);
		}
#else

		GeometryPass(commandBuffer, view, proj);
		LightingPass(commandBuffer, view, proj);

		commandBuffer.BeginRendering(
			{ utils::ColorAttachment(swapchain.GetViews()[swapchain.GetImageIndex()], VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE) },
			utils::DepthAttachment(mDepthImage->GetView(), VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE),
			utils::Scissor(swapExtent)
		);
#endif

		DrawUI();
		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.Get());

		commandBuffer.EndRendering();
		
		commandBuffer.BarrierSwapchainImage(
			swapchain.GetImages()[swapchain.GetImageIndex()],
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			VK_ACCESS_2_NONE);
	}

    void App::DrawShadowMap(CommandBuffer& commandBuffer, const glm::mat4& lightView, const glm::mat4& lightProj)
	{
		mShadowPass->Begin(commandBuffer);

		ShadowPassData pushConsts{};
		glm::mat4 viewProj = lightProj * lightView;
		for (const auto& mesh : mMeshes)
		{
			pushConsts.mvp = viewProj * mesh.transform;
			commandBuffer.PushConstants(mShadowPass->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, pushConsts);
			commandBuffer.BindVertexBuffer(*mesh.vertexBuffer);
			commandBuffer.BindIndexBuffer(*mesh.indexBuffer);
			commandBuffer.DrawIndexed(mesh.indexCount);
		}

		mShadowPass->End(commandBuffer);
	}

    void App::DrawUI()
    {
		if (ImGui::Begin("Vulkan Renderer"))
		{
			int lightNumber = 0;
			for (auto& light : mPointLights)
			{
				float color[4] = { light.i.r, light.i.g, light.i.b, light.i.a };
				const std::string label = fmt::format("Light {}", lightNumber);;
				ImGui::ColorPicker4(label.c_str(), color, 0, color);
				light.i = glm::vec4(color[0], color[1], color[2], color[3]);

				lightNumber++;
			}
			ImGui::End();
		}
    }

    void App::GeometryPass(CommandBuffer& commandBuffer, const glm::mat4& view, const glm::mat4& proj)
    {
		mGBuffers[mFrameIndex]->Begin(commandBuffer);

		mGeomPassBuffers[mFrameIndex]->SetData(GeomPassData(view, proj));

		commandBuffer.BindGraphicsPipeline(*mGeomPipe);
		commandBuffer.BindGraphicsDescriptorSets(*mGeomPipeLayout, 0, { *(mGeomSets[mFrameIndex]), mBindlessSet.Get() });

		ObjectData pushConsts{};
		for (const auto& mesh : mMeshes)
		{
			pushConsts.model = mesh.transform;
			pushConsts.albedoMapIndex = mBindlessSet.GetOrCreateId(mesh.material.albedoMap);
			pushConsts.metallicMapIndex = mBindlessSet.GetOrCreateId(mesh.material.metallicMap);
			pushConsts.roughnessMapIndex = mBindlessSet.GetOrCreateId(mesh.material.roughnessMap);
			pushConsts.normalMapIndex = mBindlessSet.GetOrCreateId(mesh.material.normalMap);
			pushConsts.aoMapIndex = mBindlessSet.GetOrCreateId(mesh.material.aoMap);
			pushConsts.emissiveMapIndex = mBindlessSet.GetOrCreateId(mesh.material.emissiveMap);

			commandBuffer.PushConstants(*mGeomPipeLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				pushConsts
			);
			commandBuffer.BindVertexBuffer(*mesh.vertexBuffer);
			commandBuffer.BindIndexBuffer(*mesh.indexBuffer);
			commandBuffer.DrawIndexed(mesh.indexCount);
		}

		mGBuffers[mFrameIndex]->End(commandBuffer);
    }

    void App::LightingPass(CommandBuffer& commandBuffer, const glm::mat4& view, const glm::mat4& proj)
    {
		const auto& swapchain = mDevice.GetSwapchain();
		commandBuffer.BeginRendering(
			{ utils::ColorAttachment(swapchain.GetViews()[swapchain.GetImageIndex()], VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE) },
			utils::DepthAttachment(mDepthImage->GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE),
			utils::Scissor(swapchain.GetExtent())
		);

		LightData lightData{};
		for (size_t i = 0; i < mPointLights.size(); ++i)
			lightData.lights[i] = { glm::vec3(view * glm::vec4(mPointLights[i].position, 1.0f)), 0, mPointLights[i].i };
		mLightPassBuffers[mFrameIndex]->SetData(lightData);

		commandBuffer.BindGraphicsPipeline(*mLightPipe);
		commandBuffer.BindGraphicsDescriptorSets(*mLightPipeLayout, 0, { *(mLightSets[mFrameIndex]) });

		LightingPassData lightPassData{};
		lightPassData.viewInverse = glm::inverse(view);
		lightPassData.viewProjInverse = glm::inverse(proj * glm::mat4(glm::mat3(view)));
		lightPassData.lightCount = mPointLights.size();
		commandBuffer.PushConstants(*mLightPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, lightPassData);
		commandBuffer.Draw(3);

		commandBuffer.EndRendering();
    }

    void App::InitWindow()
	{
		glfwSetWindowUserPointer(mWindow.Get(), this);
		glfwSetFramebufferSizeCallback(mWindow.Get(), FramebufferSizeCallback);
		glfwSetCursorPosCallback(mWindow.Get(), MousePositionCallback);
		glfwSetKeyCallback(mWindow.Get(), KeyCallback);
		glfwSetInputMode(mWindow.Get(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}

	void App::InitDepthBuffer()
	{
		const auto swapExtent = mDevice.GetSwapchain().GetExtent();
		mDepthImage = std::make_unique<Texture2D>(mDevice, mDevice.GetDepthFormat(),
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, swapExtent.width, swapExtent.height, false);

		mDevice.RunImmediateCommands([this](CommandBuffer& cmds)
		{
			cmds.Barrier(*mDepthImage,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
		});
	}

	void App::InitPipeline()
	{
		mGlobalLayout = std::make_unique<DescriptorSetLayout>(
			mDevice,
			std::initializer_list{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
			}
		);

		const auto pcRange = utils::PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(ObjectData));

		mPipeLayout = std::make_unique<PipelineLayout>(
			mDevice,
			std::initializer_list{ std::ref(*mGlobalLayout), std::ref(mBindlessSet.GetSetLayout()) },
			std::initializer_list{ pcRange }
		);

		Shader shader(mDevice, "./Assets/Shaders/Bin/PBR.spv");
		shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain");
		mPipe = std::make_unique<GraphicsPipeline>(mDevice, *mPipeLayout, shader);
		mPipe->SetVertexInput({ utils::InputBinding(0, VK_VERTEX_INPUT_RATE_VERTEX, sizeof(Vertex) )}, Vertex::GetInputAttributes())
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.AddColorAttachment(mDevice.GetSwapchain().GetFormat())
			.SetDepthAttachment(mDepthImage->GetFormat(), true)
			.Commit();

		mGeomDescLayout = std::make_unique<DescriptorSetLayout>(
			mDevice,
			std::initializer_list
			{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
			}
		);

		mGeomPipeLayout = std::make_unique<PipelineLayout>(
			mDevice,
			std::initializer_list{ std::ref(*mGeomDescLayout), std::ref(mBindlessSet.GetSetLayout()) },
			std::initializer_list{ pcRange }
		);

		Shader geomShader(mDevice, "./Assets/Shaders/Bin/GeometryPass.spv");
		geomShader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain");
		mGeomPipe = std::make_unique<GraphicsPipeline>(mDevice, *mGeomPipeLayout, geomShader);
		mGeomPipe->SetVertexInput({ utils::InputBinding(0, VK_VERTEX_INPUT_RATE_VERTEX, sizeof(Vertex) )}, Vertex::GetInputAttributes())
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.AddColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT) // Pos
			.AddColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT) // Normal
			.AddColorAttachment(VK_FORMAT_R8G8B8A8_UNORM) // Albedo
			.AddColorAttachment(VK_FORMAT_R8G8B8A8_UNORM) // Specular
			.SetDepthAttachment(mDepthImage->GetFormat(), true)
			.Commit();

		mLightDescLayout = std::make_unique<DescriptorSetLayout>(
			mDevice,
			std::initializer_list
			{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
			}
		);

		mLightPipeLayout = std::make_unique<PipelineLayout>(
			mDevice,
			std::initializer_list{ std::ref(*mLightDescLayout) },
			std::initializer_list{ utils::PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(LightingPassData)) }
		);

		Shader lightShader(mDevice, "./Assets/Shaders/Bin/LightingPass.spv");
		lightShader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain");
		mLightPipe = std::make_unique<GraphicsPipeline>(mDevice, *mLightPipeLayout, lightShader);
		mLightPipe->SetVertexInput({}, {})
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.AddColorAttachment(mDevice.GetSwapchain().GetFormat())
			.SetDepthAttachment(mDepthImage->GetFormat(), true)
			.Commit();
		
	}

	void App::InitCommandBuffers()
	{
		mCommandBuffers = mCommandPool.Allocate(MaxFramesInFlight);
	}

	void App::InitSyncPrimitives()
	{
		mAcquireSemaphores.resize(mDevice.GetSwapchain().GetViews().size());
		mRenderSemaphores.resize(mDevice.GetSwapchain().GetViews().size());
		mRenderFences.resize(MaxFramesInFlight);

		for (size_t i = 0; i < mDevice.GetSwapchain().GetViews().size(); ++i)
		{
			mAcquireSemaphores[i] = std::make_unique<Semaphore>(mDevice);
			mRenderSemaphores[i] = std::make_unique<Semaphore>(mDevice);
		}

		for (size_t i = 0; i < MaxFramesInFlight; ++i)
			mRenderFences[i] = std::make_unique<Fence>(mDevice, true);
	}

	void App::InitImGui()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		ImGui_ImplGlfw_InitForVulkan(mWindow.Get(), true);

		const auto format = mDevice.GetSwapchain().GetFormat();
		VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &format;
		renderingInfo.depthAttachmentFormat = mDevice.GetDepthFormat();

		ImGui_ImplVulkan_InitInfo imguiVulkanInfo{};
		imguiVulkanInfo.ApiVersion = VK_API_VERSION_1_4;
		imguiVulkanInfo.CheckVkResultFn = [](VkResult err) { VK_CHECK(err); };
		imguiVulkanInfo.DescriptorPoolSize = 128;
		imguiVulkanInfo.Device = mDevice.Get();
		imguiVulkanInfo.ImageCount = mDevice.GetSwapchain().GetViews().size();
		imguiVulkanInfo.MinImageCount = MaxFramesInFlight;
		imguiVulkanInfo.Instance = mDevice.GetInstance();
		imguiVulkanInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		imguiVulkanInfo.PhysicalDevice = mDevice.GetGpu();
		imguiVulkanInfo.UseDynamicRendering = true;
		imguiVulkanInfo.PipelineRenderingCreateInfo = renderingInfo;
		imguiVulkanInfo.Queue = mDevice.GetGraphicsQueue();
		imguiVulkanInfo.QueueFamily = mDevice.GetGraphicsIndex();

		ImGui_ImplVulkan_Init(&imguiVulkanInfo);
	}

	void App::InitMeshes()
	{
		// Load texture image
		mMaterial = Material{
			CreateAndStageTexture("./Assets/Models/Helmet/Default_albedo.jpg", VK_FORMAT_R8G8B8A8_SRGB, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_metalRoughness.jpg", VK_FORMAT_R8G8B8A8_SRGB, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_metalRoughness.jpg", VK_FORMAT_R8G8B8A8_SRGB, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_normal.jpg", VK_FORMAT_R8G8B8A8_SRGB, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_AO.jpg", VK_FORMAT_R8G8B8A8_SRGB, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_emissive.jpg", VK_FORMAT_R8G8B8A8_SRGB, false),
		};

		{
			auto [duckVertices, duckIndices] = utils::LoadModel("./Assets/Models/Helmet/DamagedHelmet2.gltf");
			glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
			model = glm::scale(model, glm::vec3(2.0f));
			mMeshes.emplace_back(UploadMesh(duckVertices, duckIndices, mMaterial, model));
		}

		/*{
			const std::vector<Vertex> planeVertices
			{
				{ { -0.5f, 0.0f, 0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
				{ { 0.5f, 0.0f, 0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
				{ { 0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
				{ { -0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			};

			const std::vector<uint32_t> planeIndices
			{
				0, 1, 2,
				2, 3, 0
			};

			glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
			model = glm::scale(model, glm::vec3(42.0f));
			mMeshes.emplace_back(UploadMesh(planeVertices, planeIndices, mMaterial, model));
		}*/
	}

	void App::InitUniformBuffers()
	{
		mGlobalPassBuffers.reserve(MaxFramesInFlight);
		mLightBuffers.reserve(MaxFramesInFlight);
		mGeomPassBuffers.reserve(MaxFramesInFlight);
		mLightPassBuffers.reserve(MaxFramesInFlight);
		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			mGlobalPassBuffers.emplace_back(
				std::make_unique<Buffer>(mDevice, sizeof(GlobalPassData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
			);
			mLightBuffers.emplace_back(
				std::make_unique<Buffer>(mDevice, sizeof(LightData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
			);
			mGeomPassBuffers.emplace_back(
				std::make_unique<Buffer>(mDevice, sizeof(GeomPassData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
			);
			mLightPassBuffers.emplace_back(
				std::make_unique<Buffer>(mDevice, sizeof(LightData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
			);
		}
	}

	void App::InitShadowResources()
	{
		mShadowPass = std::make_unique<ShadowPass>(mDevice, sizeof(ShadowPassData), VkExtent2D{ 2048, 2048 });
	}

	void App::InitDescriptors()
	{
		mGlobalPool = std::make_unique<DescriptorPool>(
			mDevice,
			std::initializer_list<VkDescriptorPoolSize>
			{
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * MaxFramesInFlight },
			},
			MaxFramesInFlight
		);

		mGlobalSets = mGlobalPool->Allocate({ *mGlobalLayout, *mGlobalLayout });

		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			mGlobalSets[i]->PushWrite(
					0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mGlobalPassBuffers[i]))
				.PushWrite(
					1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mLightBuffers[i]))
				.PushWrite(
					2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					mSkybox->Get(), mDevice.GetSamplers().TrilinearColor(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				.Update();
		}

		mDeferredDescPool = std::make_unique<DescriptorPool>(
			mDevice,
			std::initializer_list<VkDescriptorPoolSize>
			{
				// Geometry pass
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight },

				// Lighting pass
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * MaxFramesInFlight },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * MaxFramesInFlight },
			},
			MaxFramesInFlight * 2
		);

		mGeomSets = mDeferredDescPool->Allocate({ *mGeomDescLayout, *mGeomDescLayout });
		for (int i = 0; i < mGeomSets.size(); ++i)
			mGeomSets[i]->PushWrite(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mGeomPassBuffers[i])).Update();

		mLightSets = mDeferredDescPool->Allocate({ *mLightDescLayout, *mLightDescLayout });
		for (int i = 0; i < mLightSets.size(); ++i)
		{
			mLightSets[i]->PushWrite(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mLightPassBuffers[i]))
				.PushWrite(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mGBuffers[mFrameIndex]->GetPositionBuffer(), mDevice.GetSamplers().NearestColor(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				.PushWrite(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mGBuffers[mFrameIndex]->GetNormalBuffer(), mDevice.GetSamplers().NearestColor(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				.PushWrite(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mGBuffers[mFrameIndex]->GetAlbedoBuffer(), mDevice.GetSamplers().NearestColor(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				.PushWrite(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mGBuffers[mFrameIndex]->GetSpecularBuffer(), mDevice.GetSamplers().NearestColor(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				.PushWrite(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mSkybox->Get(), mDevice.GetSamplers().TrilinearColor(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				.Update();
		}
	}

	void App::RecreateSwapchain()
	{
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(mWindow.Get(), &width, &height);
		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(mWindow.Get(), &width, &height);
			glfwWaitEvents();
		}

		mDevice.WaitIdle();

		mDevice.GetSwapchain().Recreate();
		InitDepthBuffer();
	}

	std::unique_ptr<Texture2D> App::CreateAndStageTexture(const std::filesystem::path& path, VkFormat format, bool generateMipmaps)
	{
		stbi_set_flip_vertically_on_load(true);

		int width, height, channels;
		stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
		if (!data)
		{
			fmt::println(stderr, "Failed to load image from {}!", path);
			return nullptr;
		}

		VkDeviceSize size = width * height * 4;
		Buffer stagingTex(mDevice, size, data);

		auto resTex = std::make_unique<Texture2D>(mDevice,
			format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			width, height, generateMipmaps);

		mDevice.RunImmediateCommands([&resTex, &stagingTex, generateMipmaps](CommandBuffer& cmds)
		{
			cmds.Barrier(*resTex,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

			cmds.Copy(stagingTex, *resTex);

			if (generateMipmaps)
			{
				cmds.GenerateMipmaps(*resTex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
			}
			else
			{
				cmds.Barrier(*resTex,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
			}
		});

		return resTex;
	}

	Mesh App::UploadMesh(
		const std::vector<Vertex>& vertices,
		const std::vector<uint32_t>& indices,
		const Material& material,
		const glm::mat4& transform)
	{
		Mesh res;
		Buffer stagingVerts(mDevice, vertices.size() * sizeof(vertices[0]), vertices.data());
		Buffer stagingIdxs(mDevice, indices.size() * sizeof(indices[0]), indices.data());
		res.vertexBuffer = std::make_unique<Buffer>(
			mDevice, stagingVerts.GetSize(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
		res.indexBuffer = std::make_unique<Buffer>(
			mDevice, stagingIdxs.GetSize(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
		res.indexCount = indices.size();
		res.material = material;
		res.material.normalMap = material.normalMap;
		res.transform = transform;
		mDevice.RunImmediateCommands([this, &stagingVerts, &stagingIdxs, &res](CommandBuffer& cmds)
		{
			cmds.Copy(stagingVerts, *res.vertexBuffer);
			cmds.Copy(stagingIdxs, *res.indexBuffer);
		});
		return res;
	}

	void App::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
	{
		App* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
		app->mFramebufferResized = true;
	}

	static bool cursorEnabled = false;

	void App::MousePositionCallback(GLFWwindow* window, double xpos, double ypos)
	{
		static bool firstTouch = true;
		static double lastX;
		static double lastY;
		App* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
		if (cursorEnabled) return;

		if (firstTouch)
		{
			firstTouch = false;
			lastX = xpos;
			lastY = ypos;
		}

		float deltaX = xpos - lastX;
		float deltaY = lastY - ypos;

		constexpr float sensitivity = 0.2f;
		app->mCamera.yaw += sensitivity * deltaX;
		app->mCamera.pitch += sensitivity * deltaY;
		glfwSetCursorPos(window, lastX, lastY);
	}

    void App::KeyCallback(GLFWwindow *window, int key, int scanCode, int action, int mods)
    {
		if (key == GLFW_KEY_K && action == GLFW_PRESS)
		{
			if (cursorEnabled)
			{
				cursorEnabled = false;
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			}
			else
			{
				cursorEnabled = true;
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			}
		}
    }
}