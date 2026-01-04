#include "Renderer.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_glfw.h>
#include <stb_image.h>

#include "API/Shader.h"
#include "Utils.h"
#include "Scene.h"

namespace im
{
	Renderer::Renderer(Window& window)
		: mWindow(window)
		, mDevice(mWindow.Get())
		, mBindlessSet(mDevice)
		, mSetAllocator(mDevice)
		, mCommandPool(mDevice, mDevice.GetGraphicsIndex(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
		, mIndirectDrawBuffers{
			Buffer(mDevice,
				MaxDrawCalls * sizeof(DrawCallMesh),
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT),
			Buffer(mDevice,
				MaxDrawCalls * sizeof(DrawCallMesh),
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
		}
		, mTimestampPools{
			QueryPool(mDevice, VK_QUERY_TYPE_TIMESTAMP, 2),
			QueryPool(mDevice, VK_QUERY_TYPE_TIMESTAMP, 2),
		}
		, mCamera(0.0f, 0.0f, 0.0f, 0.0f)
	{
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(mDevice.GetGpu(), &props);
		mTimestampPeriod = props.limits.timestampPeriod;
		
		InitDepthBuffer();
		InitSyncPrimitives();
		InitCommandBuffers();
		InitPipeline();
		InitUniformBuffers();
		InitPbr();
		InitDescriptors();
	}

	Renderer::~Renderer()
	{
		mDevice.WaitIdle();

		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

    void Renderer::Render(Scene& scene)
    {
		if (!Begin()) return;
		
		BeginScene(scene);
		scene.Render();

		End();
    }

    bool Renderer::Begin()
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

		std::array<uint64_t, 2> timestampResults;
		mTimestampPools[mFrameIndex].GetResults(
			0, timestampResults.size(),
			timestampResults.size() * sizeof(timestampResults[0]),
			timestampResults.data(), sizeof(timestampResults[0]),
			VK_QUERY_RESULT_64_BIT);
		mRenderTime = (timestampResults[1] - timestampResults[0]) * mTimestampPeriod * (1.0E-6);

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		auto& commandBuffer = *mCommandBuffers[mFrameIndex];
		commandBuffer.Begin();
		commandBuffer.ResetQueryPool(mTimestampPools[mFrameIndex], 0, 2);
		commandBuffer.WriteTimestamp(mTimestampPools[mFrameIndex], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
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

	void Renderer::End()
	{
		auto& commandBuffer = *mCommandBuffers[mFrameIndex];

		mIndirectDrawBuffers[mFrameIndex].Unmap();

		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.Get());
		commandBuffer.WriteTimestamp(mTimestampPools[mFrameIndex], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
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

		mDrawCallCount = 0;
		mDrawCallPtr = nullptr;
	}

	void Renderer::BeginScene(Scene& scene)
	{
		mCamera = scene.GetCamera();
		const auto view = mCamera.GetViewMatrix();
		const auto proj = mCamera.GetProjectionMatrix();

		MainPassData passData{};
		passData.view = view;
		passData.viewProj = proj * view;
		passData.viewInverse = glm::inverse(view);
		passData.lightCount = scene.GetPointLights().size();
		mMainPassBuffers[mFrameIndex]->SetData(passData);

		LightData lightData{};
		for (int i = 0; i < scene.GetPointLights().size(); ++i)
			lightData.lights[i] = { glm::vec3(view * glm::vec4(scene.GetPointLights()[i].position, 1.0f)), 0, scene.GetPointLights()[i].i };
		mLightBuffers[mFrameIndex]->SetData(lightData);

		auto& commandBuffer = *mCommandBuffers[mFrameIndex];
		commandBuffer.BindGraphicsPipeline(*mEnvMapPipe);
		commandBuffer.BindGraphicsDescriptorSets(*mEnvMapPipeLayout, 0, { *mEnvMapSet });
		commandBuffer.PushConstants(*mEnvMapPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, CubemapData(view, proj));
		commandBuffer.Draw(3);

		mDrawCallPtr = reinterpret_cast<DrawCallMesh*>(mIndirectDrawBuffers[mFrameIndex].Map());

		commandBuffer.BindGraphicsPipeline(*mMainPipe);
		commandBuffer.BindGraphicsDescriptorSets(*mMainPipeLayout, 0, { *(mMainDescSets[mFrameIndex]), mBindlessSet.Get() });
	}

	void Renderer::DrawMesh(const Object& object)
	{
		if (mDrawCallCount > MaxDrawCalls)
		{
			fmt::println(stderr, "Exceeded maximum draw call count ({})", MaxDrawCalls);
		}

		auto& commandBuffer = *mCommandBuffers[mFrameIndex];
		mDrawCallPtr->groupsX = object.mesh->meshletCount;
		mDrawCallPtr->groupsY = 1;
		mDrawCallPtr->groupsZ = 1;
		++mDrawCallPtr;

		PbrMeshData meshData{};
		meshData.model = object.transform;
		meshData.meshletData = object.mesh->meshBuffer->GetAddress();
		meshData.vertexData = object.mesh->vertexBuffer->GetAddress();
		meshData.albedoMapIndex = mBindlessSet.GetOrCreateId(object.material.albedoMap);
		meshData.metallicMapIndex = mBindlessSet.GetOrCreateId(object.material.metallicMap);
		meshData.roughnessMapIndex = mBindlessSet.GetOrCreateId(object.material.roughnessMap);
		meshData.normalMapIndex = mBindlessSet.GetOrCreateId(object.material.normalMap);
		meshData.aoMapIndex = mBindlessSet.GetOrCreateId(object.material.aoMap);
		meshData.emissiveMapIndex = mBindlessSet.GetOrCreateId(object.material.emissiveMap);
		commandBuffer.PushConstants(*mMainPipeLayout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, meshData);

		commandBuffer.DrawMeshTasks({ object.mesh->meshletCount, 1, 1 });
		// commandBuffer.DrawMeshTasksIndirect(mIndirectDrawBuffers[mFrameIndex], sizeof(DrawCallMesh) * mDrawCallCount, 1, sizeof(DrawCallMesh));

		++mDrawCallCount;
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
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, swapExtent.width, swapExtent.height, 1);

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
					VK_SHADER_STAGE_MESH_BIT_NV | VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
			}
		);

		mMainPipeLayout = std::make_unique<PipelineLayout>(
			mDevice,
			std::initializer_list{
				std::ref(*mMainLayout),
				std::ref(mBindlessSet.GetSetLayout())
			},
			std::initializer_list<VkPushConstantRange>{
				utils::PushConstantRange(VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PbrMeshData))
			}
		);

		mMainPipe = std::make_unique<GraphicsPipeline>(
			mDevice,
			GraphicsPipelineDesc(
				*mMainPipeLayout,
				Shader(mDevice, "./Assets/Shaders/Bin/PBR.spv")
					.AddStage(VK_SHADER_STAGE_MESH_BIT_EXT, "MSMain")
					.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(mDevice.GetSwapchain().GetFormat()) },
				{ DepthStencil(mDepthImage->GetFormat()) }
			)
		);
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

    std::unique_ptr<TextureCube> Renderer::EquirectangularToCubemap(Texture2D& eqMap)
    {
		auto cubeVertexBuffer = CreateCubeVertexBuffer();

		DescriptorSetLayout cubeSetLayout(
			mDevice,
			{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
			}
		);
		PipelineLayout cubePipeLayout(
			mDevice,
			{ std::ref(cubeSetLayout) },
			{ utils::PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(EqMapData)) }
		);

		GraphicsPipeline cubePipe(
			mDevice,
			GraphicsPipelineDesc(
				cubePipeLayout,
				Shader(mDevice, "./Assets/Shaders/Bin/EquirectangularToCubemap.spv")
					.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
					.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{
					InputBinding(
						{ InputAttribute(0, VK_FORMAT_R32G32B32_SFLOAT, 0) },
						VK_VERTEX_INPUT_RATE_VERTEX,
						sizeof(float) * 3
					)
				},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT) },
				{ DepthStencil(mDepthImage->GetFormat()) }
			)
		);

		auto cubeDescSet = mSetAllocator.Allocate(cubeSetLayout);
		cubeDescSet->
			PushWrite(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eqMap, mDevice.GetSamplers().TrilinearColor())
			.Update();

		auto cubeMap = std::make_unique<TextureCube>(mDevice, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1024, 1024, true);
		std::array views
		{
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
		};
		
		mDevice.RunImmediateCommands([&](CommandBuffer& cmds)
		{
			glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
			cmds.Barrier(
				*cubeMap,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
			);
			cmds.SetViewportAndScissor({ cubeMap->GetWidth(), cubeMap->GetHeight() });
			for (uint32_t i = 0; i < 6; ++i)
			{
				cmds.BeginRendering(
					{
						utils::ColorAttachment(cubeMap->GetFaceView(i), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
					},
					utils::Scissor({ cubeMap->GetWidth(), cubeMap->GetHeight() })
				);
				cmds.BindGraphicsPipeline(cubePipe);
				cmds.BindGraphicsDescriptorSets(cubePipeLayout, 0, { std::ref(*cubeDescSet) });
				cmds.PushConstants(
					cubePipeLayout,
					VK_SHADER_STAGE_VERTEX_BIT,
					EqMapData(proj * glm::mat4(glm::mat3(views[i])))
				);
				cmds.BindVertexBuffer(cubeVertexBuffer);
				cmds.Draw(36);
				cmds.EndRendering();
			}
			cmds.Barrier(
				*cubeMap,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT
			);
		});
		return cubeMap;
    }

    std::unique_ptr<TextureCube> Renderer::CalculateDiffuseIrradiance(TextureCube& cubeMap)
    {
		auto cubeVertexBuffer = CreateCubeVertexBuffer();

		DescriptorSetLayout cubeSetLayout(
			mDevice,
			{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
			}
		);
		PipelineLayout cubePipeLayout(
			mDevice,
			{ std::ref(cubeSetLayout) },
			{ utils::PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(EqMapData)) }
		);

		GraphicsPipeline cubePipe(
			mDevice,
			GraphicsPipelineDesc(
				cubePipeLayout,
				Shader(mDevice, "./Assets/Shaders/Bin/DiffuseIrradiance.spv")
					.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
					.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{
					InputBinding(
						{ InputAttribute(0, VK_FORMAT_R32G32B32_SFLOAT, 0) },
						VK_VERTEX_INPUT_RATE_VERTEX,
						sizeof(float) * 3
					)
				},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT) },
				{ DepthStencil(mDepthImage->GetFormat()) }
			)
		);

		auto cubeDescSet = mSetAllocator.Allocate(cubeSetLayout);
		cubeDescSet->
			PushWrite(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cubeMap, mDevice.GetSamplers().TrilinearColor())
			.Update();

		auto irradianceMap = std::make_unique<TextureCube>(mDevice, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 32, 32, true);
		std::array views
		{
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
   			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
		};
		
		mDevice.RunImmediateCommands([&](CommandBuffer& cmds)
		{
			glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
			cmds.Barrier(
				*irradianceMap,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
			);
			cmds.SetViewportAndScissor({ irradianceMap->GetWidth(), irradianceMap->GetHeight() });
			for (uint32_t i = 0; i < 6; ++i)
			{
				cmds.BeginRendering(
					{
						utils::ColorAttachment(irradianceMap->GetFaceView(i), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
					},
					utils::Scissor({ irradianceMap->GetWidth(), irradianceMap->GetHeight() })
				);
				cmds.BindGraphicsPipeline(cubePipe);
				cmds.BindGraphicsDescriptorSets(cubePipeLayout, 0, { std::ref(*cubeDescSet) });
				cmds.PushConstants(
					cubePipeLayout,
					VK_SHADER_STAGE_VERTEX_BIT,
					EqMapData(proj * glm::mat4(glm::mat3(views[i])))
				);
				cmds.BindVertexBuffer(cubeVertexBuffer);
				cmds.Draw(36);
				cmds.EndRendering();
			}
			cmds.Barrier(
				*irradianceMap,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT
			);
		});
		return irradianceMap;
    }

	std::unique_ptr<TextureCube> Renderer::PrefilterEnvMap(TextureCube& cubeMap)
	{
		auto cubeVertexBuffer = CreateCubeVertexBuffer();

		DescriptorSetLayout cubeSetLayout(
			mDevice,
			{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
			}
		);
		PipelineLayout cubePipeLayout(
			mDevice,
			{ std::ref(cubeSetLayout) },
			{ utils::PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PrefilterData)) }
		);

		GraphicsPipeline cubePipe(
			mDevice,
			GraphicsPipelineDesc(
				cubePipeLayout,
				Shader(mDevice, "./Assets/Shaders/Bin/Prefilter.spv")
				.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
				.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{
					InputBinding(
						{ InputAttribute(0, VK_FORMAT_R32G32B32_SFLOAT, 0) },
						VK_VERTEX_INPUT_RATE_VERTEX,
						sizeof(float) * 3
					)
				},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT) },
				{ DepthStencil(mDepthImage->GetFormat()) }
			)
		);

		auto cubeDescSet = mSetAllocator.Allocate(cubeSetLayout);
		cubeDescSet->
			PushWrite(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cubeMap, mDevice.GetSamplers().TrilinearColor())
			.Update();

		auto res = std::make_unique<TextureCube>(
			mDevice, VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			128, 128, true, std::min(Texture::GetMaxMipLevels(128, 128), 5u));

		std::array views
		{
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
		};

		mDevice.RunImmediateCommands([&](CommandBuffer& cmds)
			{
				glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
				cmds.Barrier(
					*res,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
				);
				for (uint32_t level = 0; level < res->GetMipLevels(); ++level)
				{
					uint32_t levelWidth = res->GetWidth() * std::pow(0.5, level);
					uint32_t levelHeight = res->GetHeight() * std::pow(0.5, level);
					cmds.SetViewportAndScissor({ levelWidth, levelHeight });
					const float roughness = level / (float)(res->GetMipLevels() - 1);

					for (uint32_t i = 0; i < 6; ++i)
					{
						cmds.BeginRendering(
							{
								utils::ColorAttachment(res->GetFaceView(i, level), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
							},
							utils::Scissor({ levelWidth, levelHeight })
						);
						cmds.BindGraphicsPipeline(cubePipe);
						cmds.BindGraphicsDescriptorSets(cubePipeLayout, 0, { std::ref(*cubeDescSet) });
						cmds.PushConstants(
							cubePipeLayout,
							VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
							PrefilterData(proj * glm::mat4(glm::mat3(views[i])), roughness)
						);
						cmds.BindVertexBuffer(cubeVertexBuffer);
						cmds.Draw(36);
						cmds.EndRendering();
					}
				}
				cmds.Barrier(
					*res,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT
				);
			});
		return res;
	}

	std::unique_ptr<Texture2D> Renderer::GenerateBrdfLut()
	{
		PipelineLayout cubePipeLayout(mDevice, {}, {});

		GraphicsPipeline cubePipe(
			mDevice,
			GraphicsPipelineDesc(
				cubePipeLayout,
				Shader(mDevice, "./Assets/Shaders/Bin/IntegrateBRDF.spv")
				.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
				.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT) },
				{ DepthStencil(mDepthImage->GetFormat()) }
			)
		);

		auto res = std::make_unique<Texture2D>(
			mDevice, VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			512, 512, 1);

		mDevice.RunImmediateCommands([&](CommandBuffer& cmds)
			{
				cmds.Barrier(
					*res,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
				);
				cmds.SetViewportAndScissor({ res->GetWidth(), res->GetHeight() });
				cmds.BeginRendering(
					{
						utils::ColorAttachment(res->GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
					},
					utils::Scissor({ res->GetWidth(), res->GetHeight() })
				);
				cmds.BindGraphicsPipeline(cubePipe);
				cmds.Draw(6);
				cmds.EndRendering();
				cmds.Barrier(
					*res,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT
				);
			});
		return res;
	}

    Buffer Renderer::CreateCubeVertexBuffer()
    {
		constexpr std::array cubeVertices
		{
			// back face
			-1.0f, -1.0f, -1.0f,
			 1.0f,  1.0f, -1.0f,
			 1.0f, -1.0f, -1.0f,       
			 1.0f,  1.0f, -1.0f,
			-1.0f, -1.0f, -1.0f,
			-1.0f,  1.0f, -1.0f,
			// front face
			-1.0f, -1.0f,  1.0f,
			 1.0f, -1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,
			-1.0f,  1.0f,  1.0f,
			-1.0f, -1.0f,  1.0f,
			// left face
			-1.0f,  1.0f,  1.0f,
			-1.0f,  1.0f, -1.0f,
			-1.0f, -1.0f, -1.0f,
			-1.0f, -1.0f, -1.0f,
			-1.0f, -1.0f,  1.0f,
			-1.0f,  1.0f,  1.0f,
			// right face
			 1.0f,  1.0f,  1.0f,
			 1.0f, -1.0f, -1.0f,
			 1.0f,  1.0f, -1.0f,    
			 1.0f, -1.0f, -1.0f,
			 1.0f,  1.0f,  1.0f,
			 1.0f, -1.0f,  1.0f,  
			 // bottom face
			 -1.0f, -1.0f, -1.0f,
			  1.0f, -1.0f, -1.0f,
			  1.0f, -1.0f,  1.0f,
			  1.0f, -1.0f,  1.0f,
			 -1.0f, -1.0f,  1.0f,
			 -1.0f, -1.0f, -1.0f,
			 // top face
			 -1.0f,  1.0f, -1.0f,
			  1.0f,  1.0f , 1.0f,
			  1.0f,  1.0f, -1.0f,
			  1.0f,  1.0f,  1.0f,
			 -1.0f,  1.0f, -1.0f,
			 -1.0f,  1.0f,  1.0f,
		};

		return Buffer(
			mDevice,
			cubeVertices.size() * sizeof(cubeVertices[0]),
			cubeVertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
		);
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

	void Renderer::InitPbr()
	{
		stbi_set_flip_vertically_on_load(true);
		int width, height, channels;
		float* data = stbi_loadf("./Assets/Textures/stadium_exterior_4k.hdr", &width, &height, &channels, STBI_rgb_alpha);
		if (!data)
		{
			fmt::println(stderr, "Failed to load HDR environment map!");
			return;
		}

		mEquirectangularMap = std::make_unique<Texture2D>(mDevice, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, width, height, 1);
		Buffer staging(mDevice, width * height * 4 * sizeof(float), data);
		mDevice.RunImmediateCommands([&staging, this](CommandBuffer& cmds)
			{
				cmds.Barrier(
					*mEquirectangularMap,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
				cmds.Copy(staging, *mEquirectangularMap);
				cmds.Barrier(
					*mEquirectangularMap,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
			}
		);

		stbi_image_free(data);

		// TODO: Update view matrices for environment maps, technically incorrect now
		mEnvMap = EquirectangularToCubemap(*mEquirectangularMap);
		mIrradianceMap = CalculateDiffuseIrradiance(*mEnvMap);
		mPrefilteredEnvMap = PrefilterEnvMap(*mEnvMap);
		mBrdfLut = GenerateBrdfLut();

		// Create environment pipeline
		mEnvMapSetLayout = std::make_unique<DescriptorSetLayout>(
			mDevice,
			std::initializer_list{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			}
		);

		mEnvMapPipeLayout = std::make_unique<PipelineLayout>(
			mDevice,
			std::initializer_list{ std::ref(*mEnvMapSetLayout) },
			std::initializer_list{ utils::PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(CubemapData)) }
		);

		mEnvMapPipe = std::make_unique<GraphicsPipeline>(
			mDevice,
			GraphicsPipelineDesc(
				*mEnvMapPipeLayout,
				Shader(mDevice, "./Assets/Shaders/Bin/Cubemap.spv")
				.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
				.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(mDevice.GetSwapchain().GetFormat()) },
				{ DepthStencil(mDevice.GetDepthFormat(), false) }
			)
		);

		mEnvMapSet = mSetAllocator.Allocate(*mEnvMapSetLayout);
		mEnvMapSet->
			PushWrite(
				0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				*mEnvMap, mDevice.GetSamplers().TrilinearColor(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.Update();
	}

	void Renderer::InitDescriptors()
	{
		mMainDescSets = mSetAllocator.Allocate({ *mMainLayout, *mMainLayout }); // TODO: Convert to vector?

		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			mMainDescSets[i]->
				PushWrite(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mMainPassBuffers[i]))
				.PushWrite(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mLightBuffers[i]))
				.PushWrite(
					2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					*mIrradianceMap, mDevice.GetSamplers().TrilinearColor(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				)
				.PushWrite(
					3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					*mPrefilteredEnvMap, mDevice.GetSamplers().TrilinearColorClamp(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				)
				.PushWrite(
					4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					*mBrdfLut, mDevice.GetSamplers().TrilinearColorClamp(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				)
				.Update();
		}
	}
}