#include "Renderer.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_glfw.h>

#include "API/Shader.h"
#include "Utils.h"
#include "Skybox.h"

namespace im
{
	static constexpr uint32_t gMaxTextures = 512;

	Renderer::Renderer(Window& window)
		: mWindow(window)
		, mDevice(mWindow.Get())
		, mBindlessSet(mDevice, gMaxTextures)
		, mSetAllocator(mDevice)
		, mCommandPool(mDevice, mDevice.GetGraphicsIndex(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
		, mCamera(0.0f, 0.0f, 0.0f, 0.0f)
	{
		InitDepthBuffer();
		InitImGui();
		InitSyncPrimitives();
		InitCommandBuffers();
		InitPipeline();
		InitUniformBuffers();

		mSkybox = std::make_unique<Skybox>(
			*this,
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
	}

	Renderer::~Renderer()
	{
		mDevice.WaitIdle();

		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	bool Renderer::BeginFrame()
	{
		Swapchain& swapchain = mDevice.GetSwapchain();

		mRenderFences[mFrameIndex]->Wait();

		auto [res, imageIndex] = swapchain.AcquireNextImage(*mAcquireSemaphores[mSemaphoreIndex]);
		if (res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			RecreateSwapchain();
			return false;
		}
		else
		{
			VK_CHECK(res);
		}

		mRenderFences[mFrameIndex]->Reset();

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		auto& commandBuffer = *mCommandBuffers[mFrameIndex];
		commandBuffer.Begin();
		commandBuffer.BarrierSwapchainImage(
			swapchain.GetImages()[swapchain.GetImageIndex()],
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

		commandBuffer.SetViewportAndScissor(swapchain.GetExtent());
		commandBuffer.BeginRendering(
			{ utils::ColorAttachment(swapchain.GetViews()[swapchain.GetImageIndex()], VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE) },
			utils::DepthAttachment(mDepthImage->GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE),
			utils::Scissor(swapchain.GetExtent())
		);

		return true;
	}

	void Renderer::EndFrame()
	{
		auto& commandBuffer = *mCommandBuffers[mFrameIndex];

		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.Get());

		commandBuffer.EndRendering();

		commandBuffer.BarrierSwapchainImage(
			mDevice.GetSwapchain().GetImages()[mDevice.GetSwapchain().GetImageIndex()],
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			VK_ACCESS_2_NONE);

		mCommandBuffers[mFrameIndex]->End();

		mDevice.Submit(*(mCommandBuffers[mFrameIndex]),
			mAcquireSemaphores[mSemaphoreIndex].get(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			mRenderSemaphores[mDevice.GetSwapchain().GetImageIndex()].get(), mRenderFences[mFrameIndex].get());

		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();

		VkResult res = mDevice.GetSwapchain().Present(*mRenderSemaphores[mDevice.GetSwapchain().GetImageIndex()]);
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

	void Renderer::BeginScene(const Camera& camera, std::span<PointLight> pointLights)
	{
		mCamera = camera;
		const auto view = mCamera.GetViewMatrix();
		const auto proj = mCamera.GetProjectionMatrix();

		MainPassData passData{};
		passData.view = view;
		passData.viewProj = proj * view;
		passData.viewInverse = glm::inverse(view);
		passData.lightCount = pointLights.size();
		mMainPassBuffers[mFrameIndex]->SetData(passData);

		LightData lightData{};
		for (int i = 0; i < pointLights.size(); ++i)
			lightData.lights[i] = { glm::vec3(view * glm::vec4(pointLights[i].position, 1.0f)), 0, pointLights[i].i };
		mLightBuffers[mFrameIndex]->SetData(lightData);

		mSkybox->Draw(*mCommandBuffers[mFrameIndex], mCamera.GetViewMatrix(), mCamera.GetProjectionMatrix());
	}

	void Renderer::DrawMesh(const Mesh& mesh)
	{
		ObjectData pushConsts;
		pushConsts.model = mesh.transform;
		pushConsts.albedoMapIndex = mBindlessSet.GetOrCreateId(mesh.material.albedoMap);
		pushConsts.metallicMapIndex = mBindlessSet.GetOrCreateId(mesh.material.metallicMap);
		pushConsts.roughnessMapIndex = mBindlessSet.GetOrCreateId(mesh.material.roughnessMap);
		pushConsts.normalMapIndex = mBindlessSet.GetOrCreateId(mesh.material.normalMap);
		pushConsts.aoMapIndex = mBindlessSet.GetOrCreateId(mesh.material.aoMap);
		pushConsts.emissiveMapIndex = mBindlessSet.GetOrCreateId(mesh.material.emissiveMap);

		auto& commandBuffer = *mCommandBuffers[mFrameIndex];
		commandBuffer.BindGraphicsPipeline(*mMainPipe);
		commandBuffer.BindGraphicsDescriptorSets(*mMainPipeLayout, 0, { *(mMainDescSets[mFrameIndex]), mBindlessSet.Get() });
		commandBuffer.PushConstants(*mMainPipeLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			pushConsts
		);
		commandBuffer.BindVertexBuffer(*mesh.vertexBuffer);
		commandBuffer.BindIndexBuffer(*mesh.indexBuffer);
		commandBuffer.DrawIndexed(mesh.indexCount);
	}

	void Renderer::RecreateSwapchain()
	{
		mWindow.WaitForNonMinimized();

		mDevice.WaitIdle();
		mDevice.GetSwapchain().Recreate();
		InitDepthBuffer();
	}

	void Renderer::InitDepthBuffer()
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

	void Renderer::InitPipeline()
	{
		mMainLayout = std::make_unique<DescriptorSetLayout>(
			mDevice,
			std::initializer_list{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
			}
		);

		mMainPipeLayout = std::make_unique<PipelineLayout>(
			mDevice,
			std::initializer_list{
				std::ref(*mMainLayout),
				std::ref(mBindlessSet.GetSetLayout())
			},
			std::initializer_list{
				utils::PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(ObjectData))
			}
		);

		Shader shader(mDevice, "./Assets/Shaders/Bin/PBR.spv");
		shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain");
		mMainPipe = std::make_unique<GraphicsPipeline>(mDevice, *mMainPipeLayout, shader);
		mMainPipe->
			SetVertexInput({ Vertex::GetInputBinding(0) }, Vertex::GetInputAttributes())
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.AddColorAttachment(mDevice.GetSwapchain().GetFormat())
			.SetDepthAttachment(mDepthImage->GetFormat(), true)
			.Commit();
	}

	void Renderer::InitCommandBuffers()
	{
		mCommandBuffers = mCommandPool.Allocate(MaxFramesInFlight);
	}

	void Renderer::InitSyncPrimitives()
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

	void Renderer::InitImGui()
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

	void Renderer::InitUniformBuffers()
	{
		mMainPassBuffers.reserve(MaxFramesInFlight);
		mLightBuffers.reserve(MaxFramesInFlight);
		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			mMainPassBuffers.emplace_back(
				std::make_unique<Buffer>(mDevice, sizeof(MainPassData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
			);
			mLightBuffers.emplace_back(
				std::make_unique<Buffer>(mDevice, sizeof(LightData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
			);
		}
	}

	void Renderer::InitDescriptors()
	{
		mMainDescSets = mSetAllocator.Allocate({ *mMainLayout, *mMainLayout });

		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			mMainDescSets[i]->
				PushWrite(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mMainPassBuffers[i]))
				.PushWrite(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mLightBuffers[i]))
				.PushWrite(
					2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					mSkybox->Get(), mDevice.GetSamplers().TrilinearColor(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				)
				.Update();
		}
	}
}