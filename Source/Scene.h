#pragma once

#include "Common.h"
#include "Mesh.h"
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

        void CreateMesh(const std::filesystem::path& path);

        std::unique_ptr<Texture2D> CreateAndStageTexture(
			const std::filesystem::path& path,
			VkFormat format,
			bool generateMipmaps);

    private:
        App& mApp;

    	Material mMaterial;

		Camera mCamera;
		std::vector<MeshData> mMeshes;
        std::vector<Object> mObjects;

		std::vector<PointLight> mPointLights;
    };
}