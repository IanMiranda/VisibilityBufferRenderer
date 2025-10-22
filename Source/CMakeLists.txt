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

            ShadowPass.h
            ShadowPass.cpp

            Common.h
            Common.cpp
)

add_subdirectory(API)