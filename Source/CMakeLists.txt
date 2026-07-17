target_sources(
    VulkanApp
        PRIVATE
            Main.cpp
            
            App.h
            App.cpp

            Window.h
            Window.cpp

            Scene.h
            Scene.cpp

            Renderer.h
            Renderer.cpp

            DrawIndirectBackend.h
            DrawIndirectBackend.cpp

            VisibilityBufferBackend.h
            VisibilityBufferBackend.cpp

            Camera.h
            Camera.cpp

            Utils.h
            Utils.cpp

            Common.h
            Common.cpp

            BindlessSet.h
            BindlessSet.cpp

            DescriptorSetAllocator.h
            DescriptorSetAllocator.cpp
)

add_subdirectory(API)