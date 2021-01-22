#pragma once

#include "vkutils.h"
#include "VkTypes.h"
#include <optional>
#include "MemoryUtils.h"

namespace vkut
{
	struct ImageLoadContext 
	{
		VkDevice device;
		VmaAllocator allocator;
		VkFence uploadFence;
		VkCommandPool uploadCommandPool;
		VkQueue queue;
	};
	std::optional<AllocatedImage> loadImageFromFile(ImageLoadContext context, const char *filePath);
}