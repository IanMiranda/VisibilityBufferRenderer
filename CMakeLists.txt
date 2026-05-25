cmake_minimum_required(VERSION 3.25)

project(VulkanApp)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED 23)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)

include(FetchContent)

FetchContent_Declare(
    glfw3
    GIT_REPOSITORY  https://github.com/glfw/glfw.git
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
)

FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG a4cd45f44c4888cf064dfdfb49efc2844640899c
)

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf
    GIT_TAG 81bd50c1062fdb956e878efa2a9234b2b9ec91ec
)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt
    GIT_TAG main
)

# FetchContent_Declare(
#     ktx
#     GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software
#     GIT_TAG beef80159525d9fb7abb8645ea85f4c4f6842e8f
# )

FetchContent_MakeAvailable(glfw3 glm VulkanMemoryAllocator imgui TinyGLTF fmt)

add_executable(VulkanApp)

add_subdirectory(Source)
add_subdirectory(Vendor)

add_library(imgui)
target_sources(imgui
    PRIVATE
        ${imgui_SOURCE_DIR}/imconfig.h
        ${imgui_SOURCE_DIR}/imgui.h
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_internal.h
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imstb_rectpack.h
        ${imgui_SOURCE_DIR}/imstb_textedit.h
        ${imgui_SOURCE_DIR}/imstb_truetype.h

        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.h
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.h
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(
    imgui
        PUBLIC
            ${imgui_SOURCE_DIR}
            ${Vulkan_INCLUDE_DIRS}
            ${VulkanMemoryAllocator_SOURCE_DIR}
            ${glfw3_SOURCE_DIR}/include
)

target_include_directories(
    VulkanApp
        PRIVATE
            ${Vulkan_INCLUDE_DIRS}
            ${tinygltf_SOURCE_DIR}
            ${VulkanApp_SOURCE_DIR}/Source
)

target_compile_definitions(
    VulkanApp
        PRIVATE
            GLFW_INCLUDE_VULKAN=1
            GLM_FORCE_DEPTH_ZERO_TO_ONE=1
            GLM_ENABLE_EXPERIMENTAL=1
)

target_link_libraries(
    VulkanApp
        PRIVATE
            ${Vulkan_LIBRARIES}
            glfw
            glm::glm
            GPUOpen::VulkanMemoryAllocator
            imgui
            tinygltf
            # ktx
            fmt::fmt
)

add_custom_target(CopyAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/Assets ${CMAKE_CURRENT_BINARY_DIR}/Assets
)

add_dependencies(VulkanApp CopyAssets)