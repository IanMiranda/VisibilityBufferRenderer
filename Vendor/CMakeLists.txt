target_sources(
	VulkanApp
		PRIVATE
			VulkanMemoryAllocator/vk_mem_alloc.cpp

			stb_image/stb_image.h
			stb_image/stb_image.cpp
)

target_include_directories(
	VulkanApp
		PRIVATE
			"${CMAKE_CURRENT_LIST_DIR}/stb_image"
)