target_sources(
    VulkanApp
        PRIVATE
            Main.cpp
            
            App.h
            App.cpp

            Camera.h
            Camera.cpp

            Utils.h
            Utils.cpp

            Device.h
            Device.cpp

            Swapchain.h
            Swapchain.cpp

            Buffer.h
            Buffer.cpp

            Texture2D.h
            Texture2D.cpp

            TextureCube.h
            TextureCube.cpp

            ShadowPass.h
            ShadowPass.cpp

            DescriptorSetLayout.h
            DescriptorSetLayout.cpp

            PipelineLayout.h
            PipelineLayout.cpp

            GraphicsPipeline.h
            GraphicsPipeline.cpp

            BindlessSet.h
            BindlessSet.cpp

            Common.h
            Common.cpp
)