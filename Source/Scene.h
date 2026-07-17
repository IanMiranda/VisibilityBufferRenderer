#pragma once

#include "Common.h"
#include "Camera.h"
#include "Light.h"
#include "API/Image.h"
#include "API/ImageView.h"

namespace im
{
    class App;
    class Renderer;

    class Scene
    {
        friend App;

    public:
        Scene(Renderer& renderer);

        void Update(float deltaTime);
        void Render();

        Camera& GetCamera() { return mCamera; }
        std::vector<PointLight>& GetPointLights() { return mPointLights; }
        // Buffer& GetVertexBuffer() { return *mVertexBuffer; }
        // Buffer& GetIndexBuffer() { return *mIndexBuffer; }

    private:
    	void UpdateLightPositions();
    	void DrawUI();

        void CombineMeshBuffers();

        std::unique_ptr<Texture2D> CreateAndStageTexture(
			const std::filesystem::path& path,
			VkFormat format,
			bool generateMipmaps);

    private:
        Renderer& mRenderer;

    	Material mMaterial;
        
        // std::unique_ptr<Buffer> mVertexBuffer;
        // std::unique_ptr<Buffer> mIndexBuffer;
		
		Camera mCamera;
        std::vector<VbObject> mObjects;

		std::vector<PointLight> mPointLights;
    };
}