#include "Utils.h"

#include <fstream>
#include <tiny_gltf.h>

namespace im::utils
{
    std::vector<char> ReadFile(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            fmt::println(stderr, "Error: failed to open file with path '{}'!",
                         path);
            return {};
        }

        auto size = file.tellg();
        std::vector<char> res(size);
        file.seekg(0, std::ios::beg);
        file.read(res.data(), size);

        return res;
    }
} // namespace im::utils
