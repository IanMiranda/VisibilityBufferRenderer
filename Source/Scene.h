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
        Buffer& GetVertexBuffer() { return *mVertexBuffer; }
        Buffer& GetIndexBuffer() { return *mIndexBuffer; }

    private:
    	void UpdateLightPositions();
    	void DrawUI();

        void CombineMeshBuffers();

        std::unique_ptr<Texture2D> CreateAndStageTexture(
			const std::filesystem::path& path,
			VkFormat format,
			bool generateMipmaps);

    private:
        App& mApp;

    	Material mMaterial;

        std::unique_ptr<Buffer> mVertexBuffer;
        std::unique_ptr<Buffer> mIndexBuffer;
		
		Camera mCamera;
		std::vector<Mesh> mMeshes;
        std::vector<Object> mObjects;

		std::vector<PointLight> mPointLights;
    };
}