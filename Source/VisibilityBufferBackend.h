#pragma once

#include "Common.h"
#include "API/PipelineLayout.h"
#include "API/GraphicsPipeline.h"
#include "API/Image.h"
#include "API/CommandBuffer.h"
#include "API/ImageView.h"

namespace im
{
	class Renderer;
	class Scene;

	struct Texture2D;

	struct VbPassData
	{
		glm::mat4 modelViewProj;
		VkDeviceAddress vertexData;
	};

	class VisibilityBufferBackend
	{
	public:
		VisibilityBufferBackend(Renderer& renderer, size_t maxFramesInFlight, Image& depthImage);

		void BeginScene(CommandBuffer& cmd, ImageView& depthView, uint32_t frameIndex);
		void DrawBatch(Scene& scene, CommandBuffer& cmd, const std::vector<VbObject>& objects);
		void End(Renderer& renderer, CommandBuffer& cmd);

		void ResizeBuffers(Renderer& renderer, size_t maxFramesInFlight);

	private:
		std::vector<Texture2D> InitVisBuffers(Renderer& renderer, size_t count);
	
	private:
		PipelineLayout mVisPipeLayout;
		GraphicsPipeline mVisPipe;

		std::vector<Texture2D> mVisBuffers;
	};
}