#pragma once

#include <filesystem>

#include "Common.h"

namespace im::utils
{
    std::vector<char> ReadFile(const std::filesystem::path &path);
} // namespace im::utils