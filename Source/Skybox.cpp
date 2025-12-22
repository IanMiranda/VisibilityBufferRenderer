#include "Skybox.h"

#include <stb_image.h>

#include "Renderer.h"
#include "API/Device.h"
#include "API/Buffer.h"
#include "API/Shader.h"
#include "API/CommandBuffer.h"
#include "Utils.h"

namespace im
{
	std::array<stbi_uc*, Skybox::Faces> LoadCubemap(
		int& width,
		int& height,
		const std::array<std::filesystem::path, Skybox::Faces>& paths
	)
	{
		int channels;
		std::array<stbi_uc*, Skybox::Faces> cubemapData;
		for (size_t i = 0; i < cubemapData.size(); ++i)
		{
			stbi_uc* data = stbi_load(paths[i].string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
			if (!data)
			{
				fmt::println(stderr, "Failed to load cubemap!");
			}

			cubemapData[i] = data;
		}
		return cubemapData;
	}

	Skybox::Skybox(Renderer& renderer, const std::array<std::filesystem::path, Faces>& skyboxPaths)
	{
		stbi_set_flip_vertically_on_load(false); // Reversing UV coords using vp^-1, so images will be loaded in correct orientation

		int width, height;
		const auto cubemapData = LoadCubemap(width, height, skyboxPaths);

		constexpr VkDeviceSize bytesPerPixel = 4;
		const VkDeviceSize faceSize = width * height * bytesPerPixel;
		const VkDeviceSize size = faceSize * Faces;

		Buffer staging(renderer.GetDevice(), size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		stbi_uc* mappedData = reinterpret_cast<stbi_uc*>(staging.Map());
		for (size_t i = 0; i < Skybox::Faces; ++i)
			std::memcpy(mappedData + faceSize * i, cubemapData[i], faceSize);

		staging.Unmap();

		for (const auto& data : cubemapData)
			stbi_image_free(data);

		mEnvMap = std::make_unique<TextureCube>(renderer.GetDevice(), VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, width, height);

		renderer.GetDevice().RunImmediateCommands([this, &staging](CommandBuffer& cmds)
			{
				cmds.Barrier(*mEnvMap,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
				cmds.Copy(staging, *mEnvMap);
				cmds.Barrier(*mEnvMap,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT_KHR);
			});

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
			std::initializer_list{ utils::PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(CubemapData)) }
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

		mEnvMapSet = renderer.GetDescriptorSetAllocator().Allocate(*mEnvMapSetLayout);

		mEnvMapSet->
			PushWrite(
				0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				*mEnvMap, renderer.GetDevice().GetSamplers().TrilinearColor(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.Update();
	}

	void Skybox::Draw(CommandBuffer& cmds, const glm::mat4& view, const glm::mat4& proj) const
	{
		cmds.BindGraphicsPipeline(*mEnvMapPipe);
		cmds.BindGraphicsDescriptorSets(*mEnvMapPipeLayout, 0, { *mEnvMapSet });
		cmds.PushConstants(*mEnvMapPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, CubemapData(view, proj));
		cmds.Draw(3);
	}
}