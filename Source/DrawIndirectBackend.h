#pragma once

#include "Common.h"
#include "BindlessSet.h"
#include "DescriptorSetAllocator.h"
#include "API/PipelineLayout.h"
#include "API/GraphicsPipeline.h"
#include "API/Shader.h"
#include "API/Image.h"
#include "API/ImageView.h"
#include "API/RenderPass.h"

namespace im
{
	class Renderer;
	class Scene;

	// Uses draw indirect to collect draw calls in buffer before dispatching to GPU.
	class DrawIndirectBackend
	{
	public:
		DrawIndirectBackend(
			Renderer& renderer,
			size_t maxFramesInFlight,
			Image& depthImage
		);

		void BeginScene(Scene& scene, CommandBuffer& cmd, uint32_t frameIndex);
		void DrawBatch(const std::vector<Object>& batch);
		void End(CommandBuffer& cmd, uint32_t frameIndex);

	private:
		static constexpr uint32_t MaxDrawCalls = 100'000;

		struct DrawCall
		{
			uint32_t indexCount;
			uint32_t instanceCount;
			uint32_t firstVertex;
			uint32_t vertexOffset;
			uint32_t firstInstance;
		};

	private:
		TextureCube EquirectangularToCubemap(Renderer& renderer, Image& depthImage, ImageView& eqMap);
		TextureCube CalculateDiffuseIrradiance(Renderer& renderer, Image& depthImage, ImageView& cubeMap);
		TextureCube PrefilterEnvMap(Renderer& renderer, Image& depthImage, ImageView& cubeMap);
		Texture2D GenerateBrdfLut(Renderer& renderer, Image& depthImage);

		Buffer CreateCubeVertexBuffer(Renderer& renderer);

	private:
		BindlessSet mBindlessSet;
		DescriptorSetAllocator mSetAllocator;

		std::vector<std::unique_ptr<Buffer>> mIndirectDrawBuffers;
		std::vector<std::unique_ptr<Buffer>> mObjectDataBuffers;

		std::unique_ptr<DescriptorSetLayout> mMainLayout;
		std::unique_ptr<PipelineLayout> mMainPipeLayout;
		std::unique_ptr<GraphicsPipeline> mMainPipe;

		std::vector<std::unique_ptr<DescriptorSet>> mMainDescSets;

		TextureCube mEnvMap;
		std::unique_ptr<DescriptorSetLayout> mEnvMapSetLayout;
		std::unique_ptr<PipelineLayout> mEnvMapPipeLayout;
		std::unique_ptr<GraphicsPipeline> mEnvMapPipe;
		std::unique_ptr<DescriptorSet> mEnvMapSet;

		TextureCube mIrradianceMap;
		TextureCube mPrefilteredEnvMap;
		Texture2D mBrdfLut;

		std::vector<std::unique_ptr<Buffer>> mMainPassBuffers;
		std::vector<std::unique_ptr<Buffer>> mLightBuffers;

		DrawCall* mDrawCallPtr{ nullptr };
		ObjectData* mObjectDataPtr{ nullptr };
		uint32_t mDrawCallCount{ 0 };
		uint32_t mInstanceIndex{ 0 };
	};
}