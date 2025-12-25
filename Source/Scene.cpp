#include "Scene.h"

#include <imgui.h>
#include <stb_image.h>

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

		{
			const auto [helmetVertices, helmetIndices] = utils::LoadGltfModel("./Assets/Models/Helmet/DamagedHelmet2.gltf");

			for (float z = -5.0f; z <= 5.0f; z += 1.0f)
			{
				for (float x = -5.0f; x <= 5.0f; x += 1.0f)
				{
					glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x * 5.0f, 0.0f, z * 5.0f));
					model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
					model = glm::scale(model, glm::vec3(2.0f));
					mMeshes.emplace_back(UploadMesh(helmetVertices, helmetIndices, mMaterial, model));
				}
			}
		}

        mPointLights.resize(8);
    }
    
    void Scene::Update(float deltaTime)
    {
        if (glfwGetKey(mApp.GetWindow().Get(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(mApp.GetWindow().Get(), GLFW_TRUE);

		constexpr float moveFactor = 2.5f;
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
        for (const auto& mesh : mMeshes)
			mApp.GetRenderer().DrawMesh(mesh);

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

		auto resTex = std::make_unique<Texture2D>(mApp.GetRenderer().GetDevice(),
			format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			width, height, generateMipmaps ? Texture::GetMaxMipLevels(width, height) : 1);

		mApp.GetRenderer().GetDevice().RunImmediateCommands([&resTex, &stagingTex, generateMipmaps](CommandBuffer& cmds)
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

	Mesh Scene::UploadMesh(
		const std::vector<Vertex>& vertices,
		const std::vector<uint32_t>& indices,
		const Material& material,
		const glm::mat4& transform)
	{
		Mesh res;
		Buffer stagingVerts(mApp.GetRenderer().GetDevice(), vertices.size() * sizeof(vertices[0]), vertices.data());
		Buffer stagingIdxs(mApp.GetRenderer().GetDevice(), indices.size() * sizeof(indices[0]), indices.data());
		res.vertexBuffer = std::make_unique<Buffer>(
			mApp.GetRenderer().GetDevice(), stagingVerts.GetSize(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
		res.indexBuffer = std::make_unique<Buffer>(
			mApp.GetRenderer().GetDevice(), stagingIdxs.GetSize(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
		res.indexCount = indices.size();
		res.material = material;
		res.transform = transform;
		mApp.GetRenderer().GetDevice().RunImmediateCommands([this, &stagingVerts, &stagingIdxs, &res](CommandBuffer& cmds)
		{
			cmds.Copy(stagingVerts, *res.vertexBuffer);
			cmds.Copy(stagingIdxs, *res.indexBuffer);
		});
		return res;
	}
}