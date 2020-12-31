#pragma once
#include <vec.h>
#include "vkTypes.h"
#include <vector>
#include "OFileSerialization.h"

struct VertexInputDescription {

	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;

	VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Mesh
{
	VertexInputDescription getDescription() const;
	static std::optional<Mesh> load(const char *path);
	
	OFile data;
	AllocatedBuffer vertexBuffer{};
	AllocatedBuffer indexBuffer{};
};

