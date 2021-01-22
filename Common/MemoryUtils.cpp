#include "MemoryUtils.h"

#define VMA_IMPLEMENTATION
#include "VMA/vk_mem_alloc.h"

namespace vkmem 
{
    AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaAllocator allocator, VmaMemoryUsage memoryUsage)
    {
        const VkBufferCreateInfo bufferInfo
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = allocSize,
            .usage = usage
        };

        const VmaAllocationCreateInfo vmaallocInfo
        {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT, // the buffer will be mapped by default
            .usage = memoryUsage,
        };

        AllocatedBuffer newBuffer;

        //allocate the buffer
        VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo,
            &newBuffer.buffer,
            &newBuffer.allocation,
            nullptr));

        return newBuffer;
    }

    void destroyBuffer(VmaAllocator allocator, AllocatedBuffer buffer)
    {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    }

    VkResult createAllocator(const VmaAllocatorCreateInfo &createInfo, VmaAllocator &allocator)
    {
        return vmaCreateAllocator(&createInfo, &allocator);
    }

    void destroyAllocator(VmaAllocator allocator)
    {
        vmaDestroyAllocator(allocator);
    }

    VkResult createImage(VmaAllocator allocator, const VkImageCreateInfo &imageCreateInfo, const VmaAllocationCreateInfo &allocationCreateInfo, AllocatedImage &image, VmaAllocationInfo *allocationInfo)
    {
        return vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &image.image, &image.allocation, allocationInfo);
    }

    void destroyImage(VmaAllocator allocator, AllocatedImage image)
    {
        vmaDestroyImage(allocator, image.image, image.allocation);
    }

    void *getMappedData(AllocatedBuffer buffer)
    {
        return buffer.allocation->GetMappedData();
    }
}
