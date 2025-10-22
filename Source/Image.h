#pragma once

#include <filesystem>
#include <functional>

#include "Common.h"

namespace im
{
	struct Image
	{
		int width, height;
		std::unique_ptr<void*, std::function<void(void*)>> data;
	};

	Image LoadImage(const std::filesystem::path& path);
}