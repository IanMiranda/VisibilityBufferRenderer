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

            Texture2D.h
            Texture2D.cpp

            TextureCube.h
            TextureCube.cpp

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

            BindlessSet.h
            BindlessSet.cpp
)