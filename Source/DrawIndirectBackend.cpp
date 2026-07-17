#include "DrawIndirectBackend.h"

#include <stb_image.h>

#include "Renderer.h"
#include "Scene.h"

namespace im
{
	DrawIndirectBackend::DrawIndirectBackend(Renderer& renderer, size_t maxFramesInFlight, Image& depthImage)
		: mBindlessSet(renderer.GetDevice())
		, mSetAllocator(renderer.GetDevice())
	{
		mIndirectDrawBuffers.reserve(maxFramesInFlight);
		mObjectDataBuffers.reserve(maxFramesInFlight);

		for (size_t i = 0; i < maxFramesInFlight; ++i) {
			mIndirectDrawBuffers.emplace_back(std::make_unique<Buffer>(
				renderer.GetDevice(),
				MaxDrawCalls * sizeof(DrawCall),
				VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			));

			mObjectDataBuffers.emplace_back(std::make_unique<Buffer>(
				renderer.GetDevice(),
				MaxDrawCalls * sizeof(ObjectData),
				VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			));
		}

		mMainLayout = std::make_unique<DescriptorSetLayout>(
			renderer.GetDevice(),
			std::initializer_list{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
				DescriptorSetLayout::Binding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
			}
			);

		mMainPipeLayout = std::make_unique<PipelineLayout>(
			renderer.GetDevice(),
			std::initializer_list{
				std::ref(*mMainLayout),
				std::ref(mBindlessSet.GetSetLayout())
			},
			std::initializer_list<VkPushConstantRange>{}
		);

		mMainPipe = std::make_unique<GraphicsPipeline>(
			renderer.GetDevice(),
			GraphicsPipelineDesc(
				*mMainPipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/PBR.spv")
				.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
				.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(renderer.GetDevice().GetSwapchain().GetFormat())},
				{ DepthStencil(depthImage.GetFormat()) }
			)
		);

		mMainPassBuffers.reserve(maxFramesInFlight);
		mLightBuffers.reserve(maxFramesInFlight);
		for (size_t i = 0; i < maxFramesInFlight; ++i)
		{
			mMainPassBuffers.emplace_back(
				std::make_unique<Buffer>(renderer.GetDevice(), sizeof(MainPassData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
			);
			mLightBuffers.emplace_back(
				std::make_unique<Buffer>(renderer.GetDevice(), sizeof(LightData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
			);
		}

		stbi_set_flip_vertically_on_load(true);
		int width, height, channels;
		float* data = stbi_loadf("./Assets/Textures/stadium_exterior_4k.hdr", &width, &height, &channels, STBI_rgb_alpha);
		if (!data)
		{
			fmt::println(stderr, "Failed to load HDR environment map!");
			return;
		}

		auto eqMapImage = std::make_unique<Image>(
			renderer.GetDevice(),
			VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			width, height, 1, 1, VK_IMAGE_TYPE_2D, 1);
		auto eqMapView = std::make_unique<ImageView>(
			renderer.GetDevice(), *eqMapImage, VK_IMAGE_VIEW_TYPE_2D,
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);

		Texture2D equirectangularMap = { std::move(eqMapImage), std::move(eqMapView) };
		Buffer staging(renderer.GetDevice(), width * height * 4 * sizeof(float), data);
		renderer.GetDevice().RunImmediateCommands([&staging, &equirectangularMap, this](CommandBuffer& cmds)
			{
				cmds.Barrier(
					*equirectangularMap.image,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					equirectangularMap.view->GetAspect(), 0, 1, 0, 1
				);
				cmds.Copy(staging, *equirectangularMap.image, equirectangularMap.view->GetAspect(), 0, 1, 0);
				cmds.Barrier(
					*equirectangularMap.image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
					equirectangularMap.view->GetAspect(), 0, 1, 0, 1
				);
			}
		);

		stbi_image_free(data);

		mEnvMap = EquirectangularToCubemap(renderer, depthImage, *equirectangularMap.view);
		mIrradianceMap = CalculateDiffuseIrradiance(renderer, depthImage, *mEnvMap.view);
		mPrefilteredEnvMap = PrefilterEnvMap(renderer, depthImage, *mEnvMap.view);
		mBrdfLut = GenerateBrdfLut(renderer, depthImage);

		// Create environment pipeline
		mEnvMapSetLayout = std::make_unique<DescriptorSetLayout>(
			renderer.GetDevice(),
			std::initializer_list{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			}
		);

		mEnvMapPipeLayout = std::make_unique<PipelineLayout>(
			renderer.GetDevice(),
			std::initializer_list{ std::ref(*mEnvMapSetLayout) },
			std::initializer_list{ PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(CubemapData)) }
		);

		mEnvMapPipe = std::make_unique<GraphicsPipeline>(
			renderer.GetDevice(),
			GraphicsPipelineDesc(
				*mEnvMapPipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/Cubemap.spv")
				.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
				.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(renderer.GetDevice().GetSwapchain().GetFormat()) },
				{ DepthStencil(renderer.GetDevice().GetDepthFormat(), false) }
			)
		);

		mEnvMapSet = mSetAllocator.Allocate(*mEnvMapSetLayout);
		mEnvMapSet->
			PushWrite(
				0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				*mEnvMap.view, renderer.GetDevice().GetSamplers().TrilinearColor(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.Update();

		mMainDescSets = mSetAllocator.Allocate({ *mMainLayout, *mMainLayout }); // TODO: Convert to vector?

		for (int i = 0; i < maxFramesInFlight; ++i)
		{
			mMainDescSets[i]->
				PushWrite(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mMainPassBuffers[i]))
				.PushWrite(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *(mLightBuffers[i]))
				.PushWrite(
					2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					*mIrradianceMap.view, renderer.GetDevice().GetSamplers().TrilinearColor(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				)
				.PushWrite(
					3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					*mPrefilteredEnvMap.view, renderer.GetDevice().GetSamplers().TrilinearColorClamp(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				)
				.PushWrite(
					4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					*mBrdfLut.view, renderer.GetDevice().GetSamplers().TrilinearColorClamp(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				)
				.Update();
		}
	}

	void DrawIndirectBackend::BeginScene(Renderer& renderer, Scene& scene, CommandBuffer& cmd, ImageView& depthView, uint32_t frameIndex)
	{
		const auto& camera = scene.GetCamera();
		const auto view = camera.GetViewMatrix();
		const auto proj = camera.GetProjectionMatrix();

		const auto& swapchain = renderer.GetDevice().GetSwapchain();

		cmd.BarrierSwapchainImage(
			swapchain.GetImages()[swapchain.GetImageIndex()],
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

		cmd.SetViewportAndScissor(swapchain.GetExtent());
		cmd.BeginRendering(
			{ ColorAttachment(swapchain.GetViews()[swapchain.GetImageIndex()], VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE) },
			DepthAttachment(depthView.Get(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE),
			Scissor(swapchain.GetExtent())
		);

		cmd.BindGraphicsPipeline(*mEnvMapPipe);
		cmd.BindGraphicsDescriptorSets(*mEnvMapPipeLayout, 0, { *mEnvMapSet });
		cmd.PushConstants(*mEnvMapPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, CubemapData(view, proj));
		cmd.Draw(3);

		cmd.BindGraphicsPipeline(*mMainPipe);
		cmd.BindGraphicsDescriptorSets(*mMainPipeLayout, 0, { *(mMainDescSets[frameIndex]), mBindlessSet.Get() });
		// cmd.BindIndexBuffer(scene.GetIndexBuffer());

		MainPassData passData{};
		passData.view = view;
		passData.viewProj = proj * view;
		passData.viewInverse = glm::inverse(view);
		passData.objectData = mObjectDataBuffers[frameIndex]->GetAddress();
		// passData.vertexData = scene.GetVertexBuffer().GetAddress();
		passData.lightCount = scene.GetPointLights().size();
		mMainPassBuffers[frameIndex]->SetData(passData);

		LightData lightData{};
		for (int i = 0; i < scene.GetPointLights().size(); ++i)
			lightData.lights[i] = { glm::vec3(view * glm::vec4(scene.GetPointLights()[i].position, 1.0f)), 0, scene.GetPointLights()[i].i };
		mLightBuffers[frameIndex]->SetData(lightData);

		mDrawCallPtr = reinterpret_cast<DrawCall*>(mIndirectDrawBuffers[frameIndex]->Map());
		mObjectDataPtr = reinterpret_cast<ObjectData*>(mObjectDataBuffers[frameIndex]->Map());
	}

	void DrawIndirectBackend::DrawBatch(const std::vector<Object>& batch)
	{
		if (mDrawCallCount > MaxDrawCalls)
		{
			fmt::println(stderr, "Exceeded maximum draw call count ({})", MaxDrawCalls);
		}

		mDrawCallPtr->indexCount = batch[0].mesh->indices.size();
		mDrawCallPtr->instanceCount = batch.size();
		mDrawCallPtr->vertexOffset = batch[0].mesh->sceneBufferIndex;
		mDrawCallPtr->firstVertex = 0;
		mDrawCallPtr->firstInstance = mInstanceIndex;
		++mDrawCallPtr;

		for (const auto& object : batch)
		{
			mObjectDataPtr->model = object.transform;
			mObjectDataPtr->albedoMapIndex = mBindlessSet.GetOrCreateId(object.material.albedoMap);
			mObjectDataPtr->metallicMapIndex = mBindlessSet.GetOrCreateId(object.material.metallicMap);
			mObjectDataPtr->roughnessMapIndex = mBindlessSet.GetOrCreateId(object.material.roughnessMap);
			mObjectDataPtr->normalMapIndex = mBindlessSet.GetOrCreateId(object.material.normalMap);
			mObjectDataPtr->aoMapIndex = mBindlessSet.GetOrCreateId(object.material.aoMap);
			mObjectDataPtr->emissiveMapIndex = mBindlessSet.GetOrCreateId(object.material.emissiveMap);
			++mObjectDataPtr;
		}

		++mDrawCallCount;
		mInstanceIndex += batch.size();
	}

	void DrawIndirectBackend::End(CommandBuffer& cmd, uint32_t frameIndex)
	{
		mIndirectDrawBuffers[frameIndex]->Unmap();
		mObjectDataBuffers[frameIndex]->Unmap();

		cmd.DrawIndexedIndirect(*mIndirectDrawBuffers[frameIndex], 0, mDrawCallCount, sizeof(DrawCall));

		mDrawCallCount = 0;
		mInstanceIndex = 0;
		mDrawCallPtr = nullptr;
		mObjectDataPtr = nullptr;
	}

	TextureCube DrawIndirectBackend::EquirectangularToCubemap(Renderer& renderer, Image& depthImage, ImageView& eqMap)
	{
		auto cubeVertexBuffer = CreateCubeVertexBuffer(renderer);

		DescriptorSetLayout cubeSetLayout(
			renderer.GetDevice(),
			{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
			}
		);
		PipelineLayout cubePipeLayout(
			renderer.GetDevice(),
			{ std::ref(cubeSetLayout) },
			{ PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(EqMapData)) }
		);

		GraphicsPipeline cubePipe(
			renderer.GetDevice(),
			GraphicsPipelineDesc(
				cubePipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/EquirectangularToCubemap.spv")
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
				{ DepthStencil(depthImage.GetFormat()) }
			)
		);

		auto cubeDescSet = mSetAllocator.Allocate(cubeSetLayout);
		cubeDescSet->
			PushWrite(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eqMap, renderer.GetDevice().GetSamplers().TrilinearColor())
			.Update();

		auto cubeMap = std::make_unique<Image>(renderer.GetDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1024, 1024, 1,
			Image::CubemapFaces, VK_IMAGE_TYPE_2D, 1, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
		auto cubeMapFaces = ImageView::CreateFacesForCubemap(renderer.GetDevice(), *cubeMap, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
		std::array views
		{
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
		};

		renderer.GetDevice().RunImmediateCommands([&](CommandBuffer& cmds)
			{
				glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
				cmds.Barrier(
					*cubeMap,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0, 1
				);
				cmds.SetViewportAndScissor({ cubeMap->GetWidth(), cubeMap->GetHeight() });
				for (uint32_t i = 0; i < Image::CubemapFaces; ++i)
				{
					cmds.BeginRendering(
						{
							ColorAttachment(cubeMapFaces[i]->Get(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
						},
						Scissor({ cubeMap->GetWidth(), cubeMap->GetHeight() })
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
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0, 1
				);
			});

		auto cubeMapView = std::make_unique<ImageView>(renderer.GetDevice(), *cubeMap, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0, 1);
		return {
			std::move(cubeMap),
			std::move(cubeMapView)
		};
	}

	TextureCube DrawIndirectBackend::CalculateDiffuseIrradiance(Renderer& renderer, Image& depthImage, ImageView& cubeMap)
	{
		auto cubeVertexBuffer = CreateCubeVertexBuffer(renderer);

		DescriptorSetLayout cubeSetLayout(
			renderer.GetDevice(),
			{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
			}
		);
		PipelineLayout cubePipeLayout(
			renderer.GetDevice(),
			{ std::ref(cubeSetLayout) },
			{ PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(EqMapData)) }
		);

		GraphicsPipeline cubePipe(
			renderer.GetDevice(),
			GraphicsPipelineDesc(
				cubePipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/DiffuseIrradiance.spv")
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
				{ DepthStencil(depthImage.GetFormat()) }
			)
		);

		auto cubeDescSet = mSetAllocator.Allocate(cubeSetLayout);
		cubeDescSet->
			PushWrite(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cubeMap, renderer.GetDevice().GetSamplers().TrilinearColor())
			.Update();

		auto irradianceMap = std::make_unique<Image>(renderer.GetDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 32, 32, 1,
			Image::CubemapFaces, VK_IMAGE_TYPE_2D, 1, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
		auto irradianceMapFaces = ImageView::CreateFacesForCubemap(renderer.GetDevice(), *irradianceMap, VK_IMAGE_ASPECT_COLOR_BIT);
		std::array views
		{
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
		};

		renderer.GetDevice().RunImmediateCommands([&](CommandBuffer& cmds)
			{
				glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
				cmds.Barrier(
					*irradianceMap,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0, 1
				);
				cmds.SetViewportAndScissor({ irradianceMap->GetWidth(), irradianceMap->GetHeight() });
				for (uint32_t i = 0; i < 6; ++i)
				{
					cmds.BeginRendering(
						{
							ColorAttachment(irradianceMapFaces[i]->Get(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
						},
						Scissor({ irradianceMap->GetWidth(), irradianceMap->GetHeight() })
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
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces, 0, 1
				);
			});
		auto irradianceMapView = std::make_unique<ImageView>(
			renderer.GetDevice(),
			*irradianceMap,
			VK_IMAGE_VIEW_TYPE_CUBE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, Image::CubemapFaces, 0, 1
		);
		return {
			std::move(irradianceMap),
			std::move(irradianceMapView)
		};
	}

	TextureCube DrawIndirectBackend::PrefilterEnvMap(Renderer& renderer, Image& depthImage, ImageView& cubeMap)
	{
		auto cubeVertexBuffer = CreateCubeVertexBuffer(renderer);

		DescriptorSetLayout cubeSetLayout(
			renderer.GetDevice(),
			{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
			}
		);
		PipelineLayout cubePipeLayout(
			renderer.GetDevice(),
			{ std::ref(cubeSetLayout) },
			{ PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PrefilterData)) }
		);

		GraphicsPipeline cubePipe(
			renderer.GetDevice(),
			GraphicsPipelineDesc(
				cubePipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/Prefilter.spv")
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
				{ DepthStencil(depthImage.GetFormat())}
			)
		);

		auto cubeDescSet = mSetAllocator.Allocate(cubeSetLayout);
		cubeDescSet->
			PushWrite(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cubeMap, renderer.GetDevice().GetSamplers().TrilinearColor())
			.Update();

		auto res = std::make_unique<Image>(
			renderer.GetDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			128, 128, 1, Image::CubemapFaces, VK_IMAGE_TYPE_2D,
			std::min(Image::GetMaxMipLevels(128, 128), 5u),
			VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
		);
		auto resViews = ImageView::CreateFacesForCubemap(
			renderer.GetDevice(), *res, VK_IMAGE_ASPECT_COLOR_BIT, 0, res->GetMipLevels()
		);

		std::array views
		{
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
			glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f)),
		};

		renderer.GetDevice().RunImmediateCommands([&](CommandBuffer& cmds)
			{
				glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
				cmds.Barrier(
					*res,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces,
					0, res->GetMipLevels()
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
								ColorAttachment(resViews[level * 6 + i]->Get(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
							},
							Scissor({ levelWidth, levelHeight })
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
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, Image::CubemapFaces,
					0, res->GetMipLevels()
				);
			}
		);

		auto resView = std::make_unique<ImageView>(
			renderer.GetDevice(), *res,
			VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_ASPECT_COLOR_BIT,
			0, Image::CubemapFaces, 0, res->GetMipLevels()
		);
		return {
			std::move(res),
			std::move(resView)
		};
	}

	Texture2D DrawIndirectBackend::GenerateBrdfLut(Renderer& renderer, Image& depthImage)
	{
		PipelineLayout cubePipeLayout(renderer.GetDevice(), {}, {});

		GraphicsPipeline cubePipe(
			renderer.GetDevice(),
			GraphicsPipelineDesc(
				cubePipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/IntegrateBRDF.spv")
				.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
				.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT) },
				{ DepthStencil(depthImage.GetFormat()) }
			)
		);

		auto res = std::make_unique<Image>(
			renderer.GetDevice(), VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			512, 512, 1, 1, VK_IMAGE_TYPE_2D, 1
		);
		auto resView = std::make_unique<ImageView>(
			renderer.GetDevice(), *res,
			VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT,
			0, 1, 0, 1
		);

		renderer.GetDevice().RunImmediateCommands([&](CommandBuffer& cmds)
			{
				cmds.Barrier(
					*res,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					resView->GetAspect(), 0, 1, 0, 1
				);
				cmds.SetViewportAndScissor({ res->GetWidth(), res->GetHeight() });
				cmds.BeginRendering(
					{
						ColorAttachment(resView->Get(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
					},
					Scissor({ res->GetWidth(), res->GetHeight() })
				);
				cmds.BindGraphicsPipeline(cubePipe);
				cmds.Draw(6);
				cmds.EndRendering();
				cmds.Barrier(
					*res,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
					resView->GetAspect(), 0, 1, 0, 1
				);
			});
		return { std::move(res), std::move(resView) };
	}

	Buffer DrawIndirectBackend::CreateCubeVertexBuffer(Renderer& renderer)
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
			renderer.GetDevice(),
			cubeVertices.size() * sizeof(cubeVertices[0]),
			cubeVertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
		);
	}
}