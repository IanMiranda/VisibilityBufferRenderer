#pragma once

#include "Camera.h"
#include "Common.h"
#include "Light.h"

namespace im
{
    class App;
    class AssetManager;
    class Renderer;

    class Scene
    {
        friend App;

    public:
        Scene(AssetManager &assets, Renderer &renderer);

        void Update(float deltaTime);

        std::vector<RenderCommand> SerializeRenderCommands();

        Camera &GetCamera()
        {
            return mCamera;
        }

        std::vector<PointLight> &GetPointLights()
        {
            return mPointLights;
        }

    private:
        void UpdateLightPositions();

        std::vector<RenderCommand> GatherNodeRenderCommands(Node &node);

    private:
        Renderer &mRenderer;

        Camera mCamera;
        std::vector<std::unique_ptr<Node>> mNodes;

        std::vector<PointLight> mPointLights;
    };
} // namespace im