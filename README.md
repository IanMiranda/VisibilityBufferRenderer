# Visibility Buffer Renderer
This is a sample implementation of a visibility buffer renderer in Vulkan 1.4, along with forward and deferred rendering paths for comparison. The preferred backend can be selected at runtime.

![Sample photo of damaged helmet outside stadium exterior - PBR lighting](Media/PreviewPBR.png)

## How to build
Simply call ```cmake -B Build .``` from the root directory. This will automatically fetch dependencies. Once CMake has finished the generation phase, run ```cmake --build Build``` to build the project files from the command line.

## Project background
- C++23
- Vulkan 1.4
- Dependencies: GLFW3, GLM, VulkanMemoryAllocator, tinygltf, DearImGui, stb_image

## Features
- Three different rendering paths (forward, deferred, and visibility)
- Physically-Based Shading pipeline (metallic/roughness workflow)
- Normal mapping
- Bindless textures (via VK_EXT_descriptor_indexing)
- GLTF model loading

## References
Here is a brief list of references I used when creating this project. These are the ones I engaged with the most, though a more detailed list is on the way.
- [The Visibility Buffer: A Cache-Friendly Approach to Deferred Shading](https://jcgt.org/published/0002/02/04/)
- [Visibility Buffer and Deferred Rendering in DOOM: The Dark Ages](https://www.youtube.com/watch?v=fXakIV1OFes)
- [Visibility Buffer Rendering with Material Graphs](https://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/)
- [Real-Time Rendering, Fourth Edition](https://www.realtimerendering.com/)
- [The Slang Shading Language](https://shader-slang.org/)
- [SIGGRAPH 2013 Physically Based Shading Course Notes](https://blog.selfshadow.com/publications/s2013-shading-course/#course_content)
- [Khronos Vulkan Tutorial](https://docs.vulkan.org/tutorial/latest/00_Introduction.html)
- [LearnOpenGL](https://learnopengl.com/)
- [Cem Yuksel's Lectures](https://www.youtube.com/@cem_yuksel)