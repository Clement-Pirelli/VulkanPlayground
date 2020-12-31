#include "Mesh.h"

VertexInputDescription Mesh::getDescription() const
{
	const std::vector<AttributeType> &oFileAttributes = data.attributes();

	std::vector<VkVertexInputAttributeDescription> attributeDescriptions(oFileAttributes.size());
	size_t offset = 0;
	for (size_t i = 0; i < oFileAttributes.size(); i++)
	{
		attributeDescriptions[i] =
		{
			.location = (uint32_t)i,
			.binding = 0U,
			.format = attributeTypeToFormat(oFileAttributes[i]),
			.offset = (uint32_t)offset
		};
		offset += attributeTypeToSize(oFileAttributes[i]);
	}
	const size_t vertexSize = offset;
	return VertexInputDescription
	{
		.bindings
		{
			{
				.binding = 0U,
				.stride = (uint32_t)vertexSize,
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			}
		},
		.attributes = attributeDescriptions
	};
}

std::optional<Mesh> Mesh::load(const char *path)
{
	auto loadResult = OFile::load(path);
	if(!loadResult.has_value())
	{
		return std::nullopt;
	} else
	{
		return Mesh
		{
			.data = loadResult.value()
		};
	}
}
