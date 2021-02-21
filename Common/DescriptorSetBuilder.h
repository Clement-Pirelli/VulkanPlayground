#pragma once
#include <array>
#include <vector>
#include <vulkan/vulkan.h>
#include <optional>
#include <unordered_map>

namespace vkut
{
	class DescriptorAllocator 
	{
	public:
		DescriptorAllocator(VkDevice device);
		~DescriptorAllocator();

		void resetPools();
		[[nodiscard]] std::optional<VkDescriptorSet> allocate(VkDescriptorSetLayout layout);

		struct DescriptorTypeToSizeRatio
		{
			VkDescriptorType type;
			float ratio;
		};
		constexpr static std::array<DescriptorTypeToSizeRatio, 11> poolSizes =
		{
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f },
			DescriptorTypeToSizeRatio{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f }
		};

	private:
		void grabNewPool();

		VkDevice device;

		VkDescriptorPool currentPool{ VK_NULL_HANDLE };
		std::vector<VkDescriptorPool> usedPools;
		std::vector<VkDescriptorPool> freePools;
	};


	class DescriptorLayoutCache
	{
	public:
		DescriptorLayoutCache(VkDevice device);
		~DescriptorLayoutCache();

		[[nodiscard]] VkDescriptorSetLayout getLayout(const VkDescriptorSetLayoutCreateInfo& info);
		
	private:

		struct DescriptorLayoutInfo
		{
			std::vector<VkDescriptorSetLayoutBinding> bindings;
			bool operator ==(const DescriptorLayoutInfo &other)const;
			size_t hash() const;
		};

		struct DescriptorLayoutHash
		{
			std::size_t operator()(const DescriptorLayoutInfo &info) const 
			{
				return info.hash();
			}
		};

		std::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> layoutCache;
		VkDevice device;
	};

	class DescriptorBuilder 
	{
	public:
		DescriptorBuilder(DescriptorLayoutCache &layoutCache, DescriptorAllocator &allocator);

		struct BindingInfo
		{
			uint32_t binding;
			VkDescriptorType type;
			VkShaderStageFlags stageFlags;
		};

		[[nodiscard]] DescriptorBuilder &bindBuffer(const VkDescriptorBufferInfo &bufferInfo, BindingInfo bindingInfo);
		[[nodiscard]] DescriptorBuilder &bindImage(const VkDescriptorImageInfo &imageInfo, BindingInfo bindingInfo);

		struct BuildResult
		{
			VkDescriptorSet set;
			VkDescriptorSetLayout layout;
		};
		[[nodiscard]] std::optional<BuildResult> build(VkDevice device);
	private:

		VkDescriptorSetLayoutBinding bindingFromBindingInfo(BindingInfo info);

		std::vector<VkWriteDescriptorSet> writes;
		std::vector<VkDescriptorSetLayoutBinding> bindings;

		DescriptorLayoutCache *cache;
		DescriptorAllocator *allocator;
	};
}