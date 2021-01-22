#pragma once

#include <cstdint>
#include "VkTypes.h"

namespace vkmem
{
    [[nodiscard]]
    AllocatedBuffer createBuffer(
        size_t allocSize, 
        VkBufferUsageFlags usage, 
        VmaAllocator allocator, 
        VmaMemoryUsage memoryUsage);
    void destroyBuffer(VmaAllocator allocator, AllocatedBuffer buffer);

    [[nodiscard]]
    VkResult createAllocator(const VmaAllocatorCreateInfo &createInfo, VmaAllocator& allocator);
    void destroyAllocator(VmaAllocator allocator);

    [[nodiscard]]
    VkResult createImage(
        VmaAllocator allocator,
        const VkImageCreateInfo &imageCreateInfo,
        const VmaAllocationCreateInfo &allocationCreateInfo,
        AllocatedImage &image,
        VmaAllocationInfo *allocationInfo);
    void destroyImage(VmaAllocator, AllocatedImage image);

    void *getMappedData(AllocatedBuffer buffer);

    template<typename T>
    struct UploadInfo
    {
        const T *data;
        AllocatedBuffer buffer;
        std::optional<size_t> size;
        size_t offset = 0;

        size_t getSize() const { return size.value_or(sizeof(T)); }
    };

    template<typename T>
    void uploadToBuffer(UploadInfo<T> info)
    {
        void *address = vkmem::getMappedData(info.buffer);
        memcpy(static_cast<std::byte *>(address) + info.offset, info.data, info.getSize());
    }
};