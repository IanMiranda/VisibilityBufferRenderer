#include "Scene.h"

#include <imgui.h>
#include <stb_image.h>
#include <numeric>

#include "App.h"
#include "Utils.h"

namespace im
{
    Scene::Scene(App& app)
        : mApp(app)
        , mCamera(
			75,
			static_cast<float>(mApp.GetRenderer().GetDevice().GetSwapchain().GetExtent().width) / mApp.GetRenderer().GetDevice().GetSwapchain().GetExtent().height,
			0.1f, 100.0f
		)
    {
        mMaterial = Material{
			CreateAndStageTexture("./Assets/Models/Helmet/Default_albedo.jpg", VK_FORMAT_R8G8B8A8_SRGB, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_metalRoughness.jpg", VK_FORMAT_R8G8B8A8_UNORM, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_metalRoughness.jpg", VK_FORMAT_R8G8B8A8_UNORM, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_normal.jpg", VK_FORMAT_R8G8B8A8_UNORM, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_AO.jpg", VK_FORMAT_R8G8B8A8_UNORM, false),
			CreateAndStageTexture("./Assets/Models/Helmet/Default_emissive.jpg", VK_FORMAT_R8G8B8A8_SRGB, false),
		};
		
		const auto [helmetVertices, helmetIndices] = utils::LoadGltfModel("./Assets/Models/Helmet/DamagedHelmet2.gltf");
		mMeshes.emplace_back(helmetVertices, helmetIndices, 0);

		for (float z = -3.0f; z <= 3.0f; z += 1.0f)
		{
			for (float x = -5.0f; x <= 5.0f; x += 1.0f)
			{
				for (float y = -5.0f; y <= 5.0f; y += 1.0f)
				{
					glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x * 5.0f, y * 5.0f, z * 5.0f));
					model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
					model = glm::scale(model, glm::vec3(2.0f));
					mObjects.emplace_back(&mMeshes.back(), mMaterial, model);
				}
			}
		}

        mPointLights.resize(8);
		CombineMeshBuffers();
    }
    
    void Scene::Update(float deltaTime)
    {
        if (glfwGetKey(mApp.GetWindow().Get(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(mApp.GetWindow().Get(), GLFW_TRUE);

		const bool fast = glfwGetKey(mApp.GetWindow().Get(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

		const float moveFactor = fast ? 7.5f : 2.5f;
		const auto front = mCamera.front;
		constexpr glm::vec3 up(0.0f, 1.0f, 0.0f);
		const glm::vec3 right = glm::normalize(glm::cross(front, up));

		if (glfwGetKey(mApp.GetWindow().Get(), GLFW_KEY_W) == GLFW_PRESS)
			mCamera.position += front * deltaTime * moveFactor;
		else if (glfwGetKey(mApp.GetWindow().Get(), GLFW_KEY_S) == GLFW_PRESS)
			mCamera.position += -front * deltaTime * moveFactor;

		if (glfwGetKey(mApp.GetWindow().Get(), GLFW_KEY_A) == GLFW_PRESS)
			mCamera.position += -right * deltaTime * moveFactor;
		else if (glfwGetKey(mApp.GetWindow().Get(), GLFW_KEY_D) == GLFW_PRESS)
			mCamera.position += right * deltaTime * moveFactor;

		mCamera.UpdateFrontVector();

		UpdateLightPositions();
    }

    void Scene::Render()
    {
		mApp.GetRenderer().DrawBatch(mObjects);

		DrawUI();
    }

    void Scene::DrawUI()
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

    void Scene::UpdateLightPositions()
    {
        float offset = 0.0f;
		float distance = 20.0f * sin(glfwGetTime()) + 21.0f;
		for (auto& light : mPointLights)
		{
			light.position = glm::vec3(distance * sin(glfwGetTime() + offset), 1.0f, -distance * cos(glfwGetTime() + offset));
			offset += (2 * 3.14159) / mPointLights.size();
		}
    }

    std::unique_ptr<Texture2D> Scene::CreateAndStageTexture(
        const std::filesystem::path& path,
        VkFormat format,
        bool generateMipmaps)
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
		Buffer stagingTex(mApp.GetRenderer().GetDevice(), size, data);

		auto resTex = std::make_unique<Image>(mApp.GetRenderer().GetDevice(),
			format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			width, height, 1, 1, VK_IMAGE_TYPE_2D, generateMipmaps ? Image::GetMaxMipLevels(width, height) : 1);
		auto resTexView = std::make_unique<ImageView>(
			mApp.GetRenderer().GetDevice(),
			*resTex, VK_IMAGE_VIEW_TYPE_2D,
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, resTex->GetMipLevels()
		);

		mApp.GetRenderer().GetDevice().RunImmediateCommands([&resTex, &stagingTex, generateMipmaps](CommandBuffer& cmds)
		{
			cmds.Barrier(*resTex,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, resTex->GetMipLevels()
			);

			cmds.Copy(stagingTex, *resTex, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0);

			if (generateMipmaps)
			{
				cmds.GenerateMipmaps(
					*resTex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT
				);
			}
			else
			{
				cmds.Barrier(*resTex,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, resTex->GetMipLevels()
				);
			}
		});

		return std::make_unique<Texture2D>(std::move(resTex), std::move(resTexView));
	}

	void Scene::CombineMeshBuffers()
	{
		auto totalVertices = std::accumulate(mMeshes.begin(), mMeshes.end(), 0z, [](size_t res, const Mesh& mesh) { return res + mesh.vertices.size(); });
		auto totalIndices = std::accumulate(mMeshes.begin(), mMeshes.end(), 0z, [](size_t res, const Mesh& mesh) { return res + mesh.indices.size(); });

		Buffer stagingVtx(
			mApp.GetRenderer().GetDevice(),
			totalVertices * sizeof(Vertex),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		Buffer stagingIdx(
			mApp.GetRenderer().GetDevice(),
			totalIndices * sizeof(uint32_t),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

		auto vertexData = reinterpret_cast<Vertex*>(stagingVtx.Map());
		auto indexData = reinterpret_cast<uint32_t*>(stagingIdx.Map());
		uint32_t sceneBufferIndex = 0;
		for (auto& mesh : mMeshes)
		{
			std::memcpy(vertexData, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
			std::memcpy(indexData, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
			vertexData += mesh.vertices.size();
			indexData += mesh.indices.size();
			mesh.sceneBufferIndex = sceneBufferIndex;
			sceneBufferIndex += mesh.vertices.size();
		}
		stagingVtx.Unmap();
		stagingIdx.Unmap();

		mApp.GetRenderer().GetDevice().RunImmediateCommands([this, &stagingVtx, &stagingIdx](CommandBuffer& commandBuffer)
			{
				mVertexBuffer = std::make_unique<Buffer>(
					mApp.GetRenderer().GetDevice(),
					stagingVtx.GetSize(),
					VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
					0);
				mIndexBuffer = std::make_unique<Buffer>(
					mApp.GetRenderer().GetDevice(),
					stagingIdx.GetSize(),
					VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
					0);
				commandBuffer.Copy(stagingVtx, *mVertexBuffer);
				commandBuffer.Copy(stagingIdx, *mIndexBuffer);
			});
	}
}