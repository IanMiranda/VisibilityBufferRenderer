#pragma once

#include "Common.h"
#include "Camera.h"
#include "Light.h"

namespace im
{
    class App;

    class Scene
    {
        friend App;

    public:
        Scene(App& app);

        void Update(float deltaTime);
        void Render();

        Camera& GetCamera() { return mCamera; }
        std::vector<PointLight>& GetPointLights() { return mPointLights; }

    private:
    	void UpdateLightPositions();
    	void DrawUI();

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
        App& mApp;

    	Material mMaterial;
		
		Camera mCamera;
		std::vector<Mesh> mMeshes;

		std::vector<PointLight> mPointLights;
    };
}