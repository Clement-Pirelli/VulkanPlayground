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