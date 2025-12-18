target_sources(
    VulkanApp
        PRIVATE
            Main.cpp
            
            App.h
            App.cpp

            Window.h
            Window.cpp

            Renderer.h
            Renderer.cpp

            Skybox.h
            Skybox.cpp

            Camera.h
            Camera.cpp

            Utils.h
            Utils.cpp

            ShadowPass.h
            ShadowPass.cpp

            GBuffer.h
            GBuffer.cpp

            Common.h
            Common.cpp

            Geometry.h

            BindlessSet.h
            BindlessSet.cpp

            DescriptorSetAllocator.h
            DescriptorSetAllocator.cpp
)

add_subdirectory(API)