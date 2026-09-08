target_sources(
    VulkanApp
        PRIVATE
            Main.cpp
            
            App.h
            App.cpp

            Window.h
            Window.cpp

            AssetManager.h
            AssetManager.cpp

            Scene.h
            Scene.cpp

            Renderer.h
            Renderer.cpp

            ForwardBackend.h
            ForwardBackend.cpp

            DeferredBackend.h
            DeferredBackend.cpp

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