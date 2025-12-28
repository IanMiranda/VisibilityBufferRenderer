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
		
		CreateMesh("./Assets/Models/Helmet/DamagedHelmet2.gltf");

#if 0
		{
			for (float z = -10.0f; z <= 10.0f; z += 1.0f)
			{
				for (float x = -10.0f; x <= 10.0f; x += 1.0f)
				{
					for (float y = -10.0f; y <= 10.0f; y += 1.0f)
					{
						glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x * 0.5f, y * 0.5f, z * 0.5f));
						model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
						model = glm::scale(model, glm::vec3(2.0f));
						mObjects.emplace_back(&mMeshes.back(), mMaterial, model);
					}
				}
			}
		}
#else
		glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
		model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		model = glm::scale(model, glm::vec3(2.0f));
		mObjects.emplace_back(&mMeshes.back(), mMaterial, model);
#endif
        mPointLights.resize(8);
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
		for (const auto& object : mObjects)
			mApp.GetRenderer().DrawMesh(object);

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

	void Scene::CreateMesh(const std::filesystem::path& path)
	{
		const auto [vertices, indices] = utils::LoadGltfModel(path.string().c_str());
		const auto meshlets = utils::BuildMeshlets(vertices, indices);

		Buffer stagingVtx(
			mApp.GetRenderer().GetDevice(),
			vertices.size() * sizeof(Vertex),
			vertices.data(),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		Buffer stagingMsh(
			mApp.GetRenderer().GetDevice(),
			meshlets.size() * sizeof(Meshlet),
			meshlets.data(),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

		mApp.GetRenderer().GetDevice().RunImmediateCommands([this, &stagingVtx, &stagingMsh, &meshlets](CommandBuffer& commandBuffer)
			{
				auto vbo = std::make_unique<Buffer>(
					mApp.GetRenderer().GetDevice(),
					stagingVtx.GetSize(),
					VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
					0);
				auto mbo = std::make_unique<Buffer>(
					mApp.GetRenderer().GetDevice(),
					stagingMsh.GetSize(),
					VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
					0);
				commandBuffer.Copy(stagingVtx, *vbo);
				commandBuffer.Copy(stagingMsh, *mbo);
				mMeshes.push_back({ std::move(vbo), std::move(mbo), static_cast<uint32_t>(meshlets.size()) });
			}
		);
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
}