target_sources(
    VulkanApp
        PRIVATE
            Device.h
            Device.cpp

            Swapchain.h
            Swapchain.cpp

            CommandPool.h
            CommandPool.cpp

            CommandBuffer.h
            CommandBuffer.cpp

            Fence.h
            Fence.cpp

            Semaphore.h
            Semaphore.cpp

            Buffer.h
            Buffer.cpp

            Image.h
            Image.cpp

            ImageView.h
            ImageView.cpp

            Sampler.h
            Sampler.cpp

            Shader.h
            Shader.cpp

            DescriptorSetLayout.h
            DescriptorSetLayout.cpp

            PipelineLayout.h
            PipelineLayout.cpp

            GraphicsPipeline.h
            GraphicsPipeline.cpp

            DescriptorPool.h
            DescriptorPool.cpp

            DescriptorSet.h
            DescriptorSet.cpp
)