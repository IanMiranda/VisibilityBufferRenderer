cmake_minimum_required(VERSION 3.25)

project(VulkanApp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(VulkanApp)

add_subdirectory(Source)
add_subdirectory(Vendor)

find_package(Vulkan REQUIRED)

include(FetchContent)

FetchContent_Declare(
    glfw3
    GIT_REPOSITORY  https://github.com/glfw/glfw.git
    GIT_TAG         7b6aead9fb88b3623e3b3725ebb42670cbe4c579
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG 0af55ccecd98d4e5a8d1fad7de25ba429d60e863
)

FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG 1d8f600fd424278486eade7ed3e877c99f0846b1
)

FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader
    GIT_TAG a4e519b0a0f29c790464fcfeadfe25a7f9fa15ff
)

FetchContent_MakeAvailable(glfw3 glm VulkanMemoryAllocator tinyobjloader)

target_include_directories(
    VulkanApp
        PRIVATE
            ${Vulkan_INCLUDE_DIRS}
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
            tinyobjloader
)

add_custom_target(CopyAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/Assets ${CMAKE_CURRENT_BINARY_DIR}/Assets
)

add_dependencies(VulkanApp CopyAssets)