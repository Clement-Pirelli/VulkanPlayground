#pragma once
#include "VMA/vk_mem_alloc.h"
#include "vkutils.h"

struct AllocatedBuffer 
{
    VkBuffer buffer;
    VmaAllocation allocation;
};

struct AllocatedImage {
    VkImage image;
    VmaAllocation allocation;
};

//todo: use this for buffer uploads etc
struct BufferSpan
{
    AllocatedBuffer buffer;
    size_t size;
    size_t offset;
};

struct Texture {
    AllocatedImage image;
    VkImageView imageView;
};