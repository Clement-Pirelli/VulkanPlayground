#pragma once
#include <cstdint>
#include "vec.h"
#include "vulkan/vulkan.h"
#include <assert.h>

enum class AttributeType : uint8_t
{
	vec3,
	vec2,
	float32,
	uint32,
};

[[nodiscard]]
inline VkFormat attributeTypeToFormat(const AttributeType attributeType)
{
	switch (attributeType)
	{
	case AttributeType::vec3:
		return VK_FORMAT_R32G32B32_SFLOAT;
		break;
	case AttributeType::vec2:
		return VK_FORMAT_R32G32_SFLOAT;
		break;
	case AttributeType::float32:
		return VK_FORMAT_R32_SFLOAT;
		break;
	case AttributeType::uint32:
		return VK_FORMAT_R32_UINT;
		break;
	default:
		assert(false);
		return VK_FORMAT_UNDEFINED;
		break;
	}
}

[[nodiscard]]
inline size_t attributeTypeToSize(const AttributeType attributeType)
{
	switch (attributeType)
	{
	case AttributeType::vec3:
		return sizeof(vec3);
		break;
	case AttributeType::vec2:
		return sizeof(vec2);
		break;
	case AttributeType::float32:
		return sizeof(float);
		break;
	case AttributeType::uint32:
		return sizeof(uint32_t);
		break;
	default:
		assert(false);
		return 0;
		break;
	}
}