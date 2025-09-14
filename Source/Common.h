#pragma once

#include <iostream>
#include <vector>
#include <string>

#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#define VK_CHECK(x) \
	do \
	{ \
		VkResult _result = (x); \
		if (_result != VK_SUCCESS) \
		{ \
			std::cerr << "Error at line " << __LINE__ << " in file " << __FILE__ << ": " << _result << '\n'; \
		} \
	} while(0)