#pragma once

#include <deque>

#include "API/DescriptorPool.h"
#include "API/DescriptorSet.h"
#include "API/DescriptorSetLayout.h"
#include "Common.h"

namespace im
{
    class Device;
    struct Texture2D;

    class BindlessSet
    {
    public:
        static constexpr uint32_t MaxTextures{100'000};

    public:
        BindlessSet(Device &device, uint32_t maxTextures = MaxTextures);

        DescriptorSetLayout &GetSetLayout()
        {
            return mBindlessSetLayout;
        }
        DescriptorSet &Get()
        {
            return *mBindlessSet;
        }

        uint32_t GetOrCreateId(std::shared_ptr<Texture2D> texture);

    private:
        Device &mDevice;
        const uint32_t mMaxTextures;

        DescriptorPool mBindlessPool;
        DescriptorSetLayout mBindlessSetLayout;
        std::unique_ptr<DescriptorSet> mBindlessSet;

        std::deque<uint32_t> mTexFreeList;
        std::unordered_map<std::shared_ptr<Texture2D>, uint32_t> mTexMap;
    };
} // namespace im