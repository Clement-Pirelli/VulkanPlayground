#include "DescriptorSetBuilder.h"
#include "vkutils.h"
#include <algorithm>

namespace vkut
{
	constexpr VkDescriptorPoolCreateFlags noCreateFlags = 0;
	constexpr int maxSetsPerPool = 1000;
	constexpr auto compareBindings = [](VkDescriptorSetLayoutBinding &a, VkDescriptorSetLayoutBinding &b)
	{
		return a.binding < b.binding;
	};

	[[nodiscard]] VkDescriptorPool createPool(VkDevice device, size_t count, VkDescriptorPoolCreateFlags flags)
	{
		std::vector<VkDescriptorPoolSize> sizes;
		sizes.reserve(DescriptorAllocator::poolSizes.size());
		for (auto sz : DescriptorAllocator::poolSizes) {
			sizes.push_back({ sz.type, uint32_t(sz.ratio * count) });
		}
		const VkDescriptorPoolCreateInfo pool_info
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = flags,
			.maxSets = static_cast<uint32_t>(count),
			.poolSizeCount = (uint32_t)sizes.size(),
			.pPoolSizes = sizes.data(),
		};

		VkDescriptorPool descriptorPool;
		VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool));
		return descriptorPool;
	}


	DescriptorAllocator::DescriptorAllocator(VkDevice givenDevice) : device(givenDevice)
	{}

	DescriptorAllocator::~DescriptorAllocator()
	{
		for (VkDescriptorPool p : freePools)
		{
			vkDestroyDescriptorPool(device, p, nullptr);
		}
		freePools.clear();
		for (VkDescriptorPool p : usedPools)
		{
			vkDestroyDescriptorPool(device, p, nullptr);
		}
		
		if(currentPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, currentPool, nullptr);
		}

		usedPools.clear();
	}

	void DescriptorAllocator::resetPools()
	{
		//reset every pool
		for (auto p : usedPools) {
			vkResetDescriptorPool(device, p, noCreateFlags);
		}

		//move all pools to the reusable vector
		freePools = usedPools;
		usedPools.clear();

		currentPool = VK_NULL_HANDLE;
	}

	std::optional<VkDescriptorSet> DescriptorAllocator::allocate(VkDescriptorSetLayout layout)
	{
		//initialize the currentPool handle if its null
		if (currentPool == VK_NULL_HANDLE)
		{
			grabNewPool();
		}

		VkDescriptorSetAllocateInfo allocInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = currentPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &layout,
		};

		//try to allocate the descriptor set
		VkDescriptorSet set;
		const VkResult allocResult = vkAllocateDescriptorSets(device, &allocInfo, &set);

		if (allocResult == VK_SUCCESS)
		{
			return set;
		}
		else if (!(allocResult == VK_ERROR_FRAGMENTED_POOL || allocResult == VK_ERROR_OUT_OF_POOL_MEMORY))
		{
			return std::nullopt;
		}
		else
		{
			//allocate a new pool and retry
			grabNewPool();
			allocInfo.descriptorPool = currentPool;

			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &set));
			return set;
		}
	}

	void DescriptorAllocator::grabNewPool()
	{
		if(currentPool != VK_NULL_HANDLE)
		{
			usedPools.push_back(currentPool);
		}

		const VkDescriptorPool pool = [this]()
		{
			if (freePools.size() > 0)
			{
				//grab pool from the back of the vector and remove it from there
				const VkDescriptorPool pool = freePools.back();
				freePools.pop_back();
				return pool;
			}
			else
			{
				return createPool(device, maxSetsPerPool, noCreateFlags);
			}
		}(); //immediately invoked lambda

		currentPool = pool;
	}



	DescriptorLayoutCache::DescriptorLayoutCache(VkDevice givenDevice) : device(givenDevice)
	{
	}

	DescriptorLayoutCache::~DescriptorLayoutCache()
	{
		for (auto pair : layoutCache) {
			vkDestroyDescriptorSetLayout(device, pair.second, nullptr);
		}
		layoutCache.clear();
	}

	VkDescriptorSetLayout DescriptorLayoutCache::getLayout(const VkDescriptorSetLayoutCreateInfo &info)
	{
		DescriptorLayoutInfo layoutinfo;
		layoutinfo.bindings.reserve(info.bindingCount);

		for (size_t i = 0; i < info.bindingCount; i++) 
		{
			layoutinfo.bindings.push_back(info.pBindings[i]);
		}
		std::sort(layoutinfo.bindings.begin(), layoutinfo.bindings.end(), compareBindings);

		auto it = layoutCache.find(layoutinfo);
		if (it != layoutCache.end()) 
		{
			return (*it).second;
		}
		else 
		{
			VkDescriptorSetLayout layout;
			vkCreateDescriptorSetLayout(device, &info, nullptr, &layout);

			//add to cache
			layoutCache[layoutinfo] = layout;
			return layout;
		}
	}

	bool DescriptorLayoutCache::DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo &other) const
	{
		if (other.bindings.size() != bindings.size()) 
		{
			return false;
		}
		else 
		{
			//bindings are sorted, so they'll match
			for (int i = 0; i < bindings.size(); i++) {
				const VkDescriptorSetLayoutBinding &otherBinding = other.bindings[i];
				const VkDescriptorSetLayoutBinding &thisBinding = bindings[i];
				const bool differentBinding = otherBinding.binding != thisBinding.binding;
				const bool differentType = otherBinding.descriptorType != thisBinding.descriptorType;
				const bool differentCount = otherBinding.descriptorCount != thisBinding.descriptorCount;
				const bool differentFlags =  otherBinding.stageFlags != thisBinding.stageFlags;
				if (differentBinding
					|| differentType
					|| differentCount
					|| differentFlags)
				{
					return false;
				}
			}
			return true;
		}
	}
	size_t DescriptorLayoutCache::DescriptorLayoutInfo::hash() const
	{
		using std::size_t;
		using std::hash;

		size_t result = hash<size_t>()(bindings.size());
		for (const VkDescriptorSetLayoutBinding &b : bindings)
		{
			size_t binding_hash = b.binding | b.descriptorType << 8 | b.descriptorCount << 16 | b.stageFlags << 24;
			result ^= hash<size_t>()(binding_hash);
		}
		return result;
	}



	DescriptorBuilder::DescriptorBuilder(DescriptorLayoutCache &givenLayoutCache, DescriptorAllocator &givenAllocator) : cache(&givenLayoutCache), allocator(&givenAllocator)
	{
	}

	DescriptorBuilder &DescriptorBuilder::bindBuffer(const VkDescriptorBufferInfo &bufferInfo, BindingInfo bindingInfo)
	{
		bindings.push_back(bindingFromBindingInfo(bindingInfo));

		const VkWriteDescriptorSet newWrite
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = bindingInfo.binding,
			.descriptorCount = 1,
			.descriptorType = bindingInfo.type,
			.pBufferInfo = &bufferInfo,
		};

		writes.push_back(newWrite);
		return *this;
	}
	DescriptorBuilder &DescriptorBuilder::bindImage(const VkDescriptorImageInfo &imageInfo, BindingInfo bindingInfo)
	{
		bindings.push_back(bindingFromBindingInfo(bindingInfo));

		//create the descriptor write
		const VkWriteDescriptorSet newWrite
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = bindingInfo.binding,
			.descriptorCount = 1,
			.descriptorType = bindingInfo.type,
			.pImageInfo = &imageInfo,
		};

		writes.push_back(newWrite);
		return *this;
	}
	std::optional<DescriptorBuilder::BuildResult> DescriptorBuilder::build(VkDevice device)
	{
		const VkDescriptorSetLayoutCreateInfo layoutInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data(),
		};

		const VkDescriptorSetLayout layout = cache->getLayout(layoutInfo);
		const std::optional<VkDescriptorSet> set = allocator->allocate(layout);
		if (!set.has_value()) 
		{
			return std::nullopt; 
		}
		else
		{
			for (VkWriteDescriptorSet &w : writes) {
				w.dstSet = set.value();
			}
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
			return BuildResult{ .set = set.value(), .layout = layout };
		}
	}

	VkDescriptorSetLayoutBinding DescriptorBuilder::bindingFromBindingInfo(BindingInfo info)
	{
		return {
			.binding = info.binding,
			.descriptorType = info.type,
			.descriptorCount = 1,
			//pImmutableSamplers is nullptr
			.stageFlags = info.stageFlags,
		};
	}
}