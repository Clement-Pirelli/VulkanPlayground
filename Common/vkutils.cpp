#include "vkutils.h"
#include <assert.h>
#include <string>
#include <set>
#include "Files.h"

#pragma warning(disable : 4100)
#pragma warning(disable : 4702)

namespace vkut {
	namespace {

#pragma region SMALL_UTILITIES
		template<typename T>
		void setFunctionPointer(VkDevice device, T &pointer, const char *name)
		{
			pointer = reinterpret_cast<T>(vkGetDeviceProcAddr(device, name));
		}

#define VK_SET_FUNC_PTR(device, func) (setFunctionPointer(device, func, #func))


		template <class T>
		T min(const T &a, const T &b)
		{
			return (a < b) ? a : b;
		}

		template <class T>
		T max(const T &a, const T &b)
		{
			return (a > b) ? a : b;
		}
#pragma endregion

		VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
		{
			for (size_t i = 0; i < candidates.size(); i++) {
				VkFormatProperties props = {};
				vkGetPhysicalDeviceFormatProperties(physicalDevice, candidates[i], &props);

				if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
					return candidates[i];
				}
				else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
					return candidates[i];
				}
			}

			Logger::logError("Couldn't find format!");
			assert(false);
			return VkFormat::VK_FORMAT_UNDEFINED;
		}

		uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
		{
			VkPhysicalDeviceMemoryProperties memProperties = {};
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
					return i;
				}
			}

			Logger::logError("Failed to find suitable memory type!");
			assert(false);
			return ~0U;
		}

		VkCommandBuffer initSingleTimeCommands(VkDevice device, VkCommandPool commandPool) 
		{
			const VkCommandBufferAllocateInfo allocInfo 
			{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = commandPool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};

			VkCommandBuffer commandBufferData = {};
			vkAllocateCommandBuffers(device, &allocInfo, &commandBufferData);

			const VkCommandBufferBeginInfo beginInfo 
			{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
			};

			vkBeginCommandBuffer(commandBufferData, &beginInfo);

			return commandBufferData;
		}

		void submitSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue) {
			vkEndCommandBuffer(commandBuffer);

			const VkSubmitInfo submitInfo
			{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.commandBufferCount = 1,
				.pCommandBuffers = &commandBuffer
			};

			vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(queue);

			vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
		}

		uint64_t GetBufferAddress(VkDevice device, VkBuffer buffer)
		{
			static PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = nullptr;

			if (vkGetBufferDeviceAddressKHR == nullptr)
				VK_SET_FUNC_PTR(device, vkGetBufferDeviceAddressKHR);

			const VkBufferDeviceAddressInfoKHR bufferAddressInfo
			{
				.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
				.pNext = nullptr,
				.buffer = buffer,
			};

			return vkGetBufferDeviceAddressKHR(device, &bufferAddressInfo);
		}

	}


	std::optional<VkShaderModule> createShaderModule(VkDevice device, const char *filePath)
	{
		FileReader reader = FileReader(std::string(filePath));

		if (reader.failed()) return std::nullopt;

		const std::string code = reader.readInto<std::string>();

		const VkShaderModuleCreateInfo createInfo
		{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = code.size(),
			.pCode = reinterpret_cast<const uint32_t *>(code.data())
		};

		VkShaderModule shaderModule = {};
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			return std::nullopt;
		}
		Logger::logTrivialFormatted("Created shader module %u!", shaderModule);

		return shaderModule;
	}

	void destroyShaderModule(VkDevice device, VkShaderModule shaderModule)
	{
		vkDestroyShaderModule(device, shaderModule, nullptr);
		Logger::logTrivialFormatted("Destroyed shader module %u!", shaderModule);
	}

	//from: https://github.com/SaschaWillems/Vulkan/tree/master/examples/dynamicuniformbuffer
	size_t padUniformBufferSize(size_t originalSize, const VkPhysicalDeviceProperties &deviceProperties)
	{
		// Calculate required alignment based on minimum device offset alignment
		const size_t minUboAlignment = deviceProperties.limits.minUniformBufferOffsetAlignment;
		if (minUboAlignment > 0) {
			return (originalSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
		}
		else 
		{
			return originalSize;
		}
	}

	VkFramebuffer createRenderPassFramebuffer(const CreateRenderPassFramebufferInfo &info)
	{
		assert(info.colorViews.size() != 0);
		VkFramebuffer framebuffer = {};

		std::vector<VkImageView> attachments = std::vector<VkImageView>(info.colorViews);
		if (info.depthAttachment.has_value()) attachments.push_back(info.depthAttachment.value());

		const VkFramebufferCreateInfo framebufferInfo
		{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = info.renderPass,
			.attachmentCount = static_cast<uint32_t>(attachments.size()),
			.pAttachments = attachments.data(),
			.width = info.width,
			.height = info.height,
			.layers = 1
		};

		VK_CHECK(vkCreateFramebuffer(info.device, &framebufferInfo, nullptr, &framebuffer));
		Logger::logMessageFormatted("Created framebuffer %u with renderpass %u! ", framebuffer, info.renderPass);

		return framebuffer;
	}

	void destroyFramebuffer(VkDevice device, VkFramebuffer framebuffer)
	{
		vkDestroyFramebuffer(device, framebuffer, nullptr);
		Logger::logMessageFormatted("Destroyed framebuffer %u! ", framebuffer);
	}

	VkDescriptorPool createDescriptorPool(VkDevice device, const std::vector<VkDescriptorType> &descriptorTypes, uint32_t descriptorCount, uint32_t maxSets)
	{
		std::vector<VkDescriptorPoolSize> poolSizes = std::vector<VkDescriptorPoolSize>(descriptorTypes.size());
		for (size_t i = 0; i < poolSizes.size(); i++)
		{
			const VkDescriptorPoolSize poolSize
			{
				.type = descriptorTypes[i],
				.descriptorCount = descriptorCount
			};
			poolSizes[i] = poolSize;
		}

		const VkDescriptorPoolCreateInfo poolInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = maxSets,
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data(),
		};

		VkDescriptorPool descriptorPool = {};
		VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
		Logger::logMessageFormatted("Created descriptor pool %u! ", descriptorPool);
		return descriptorPool;
	}

	//implicitly destroys all descriptor sets from this pool
	void destroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool)
	{
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		Logger::logMessageFormatted("Destroyed descriptor pool %u! ", descriptorPool);
	}

	VkDescriptorSet createDescriptorSet(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorPool descriptorPool, const std::vector<DescriptorSetInfo> &descriptorSetInfos)
	{
		const VkDescriptorSetAllocateInfo allocInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &descriptorSetLayout
		};

		VkDescriptorSet descriptorSet = {};

		VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		Logger::logMessageFormatted("Allocated descriptor set %u!", descriptorSet);

		std::vector<VkWriteDescriptorSet> descriptorWrites = std::vector<VkWriteDescriptorSet>(descriptorSetInfos.size());
		for (size_t i = 0; i < descriptorWrites.size(); i++)
		{
			const DescriptorSetInfo &info = descriptorSetInfos[i];

			const VkWriteDescriptorSet descriptorWrite
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = info.pNext,
				.dstSet = descriptorSet,
				.dstBinding = info.dstBinding,
				.dstArrayElement = info.dstArrayElement,
				.descriptorCount = info.descriptorCount,
				.descriptorType = info.descriptorType,
				.pImageInfo = info.pImageInfo,
				.pBufferInfo = info.pBufferInfo,
				.pTexelBufferView = info.pTexelBufferView,
			};

			descriptorWrites[i] = descriptorWrite;
		}

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
		Logger::logMessageFormatted("Updated descriptor set %u! ", descriptorSet);

		return descriptorSet;
	}

	VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding> &bindings)
	{
		const VkDescriptorSetLayoutCreateInfo layoutInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};

		VkDescriptorSetLayout descriptorSetLayout = {};
		VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));

		Logger::logMessageFormatted("Created descriptor set layout %u! ", descriptorSetLayout);

		return descriptorSetLayout;
	}

	void destroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout)
	{
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		Logger::logMessageFormatted("Destroyed descriptor set layout %u! ", descriptorSetLayout);
	}

	VkSampleCountFlagBits getMaxImageSamples(VkPhysicalDevice physicalDevice)
	{
		static VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
		if (sampleCount != VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM) return sampleCount;

		VkPhysicalDeviceProperties physicalDeviceProperties;
		vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

		VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
		if (counts & VK_SAMPLE_COUNT_64_BIT) { sampleCount = VK_SAMPLE_COUNT_64_BIT; }
		else
			if (counts & VK_SAMPLE_COUNT_32_BIT) { sampleCount = VK_SAMPLE_COUNT_32_BIT; }
			else
				if (counts & VK_SAMPLE_COUNT_16_BIT) { sampleCount = VK_SAMPLE_COUNT_16_BIT; }
				else
					if (counts & VK_SAMPLE_COUNT_8_BIT) { sampleCount = VK_SAMPLE_COUNT_8_BIT; }
					else
						if (counts & VK_SAMPLE_COUNT_4_BIT) { sampleCount = VK_SAMPLE_COUNT_4_BIT; }
						else
							if (counts & VK_SAMPLE_COUNT_2_BIT) { sampleCount = VK_SAMPLE_COUNT_2_BIT; }
							else
							{
								sampleCount = VK_SAMPLE_COUNT_1_BIT;
							}

		return sampleCount;
	}

	VkFormat findDepthFormat(VkPhysicalDevice physicalDevice)
	{
		return findSupportedFormat(
			physicalDevice,
			std::vector<VkFormat> { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);
	}

	bool hasStencilComponent(VkFormat format)
	{
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
	}


	VkPipeline createPipeline(const PipelineInfo &pipelineInfo)
	{
		const VkPipelineViewportStateCreateInfo viewportState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.pViewports = &pipelineInfo.viewport,
			.scissorCount = 1,
			.pScissors = &pipelineInfo.scissor,

		};

		//setup dummy color blending. We arent using transparent objects yet
		//the blending is just "no blend", but we do write to the color attachment
		const VkPipelineColorBlendStateCreateInfo colorBlending
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.logicOp = VK_LOGIC_OP_COPY,
			.attachmentCount = 1,
			.pAttachments = &pipelineInfo.colorBlendAttachment,
		};

		VkGraphicsPipelineCreateInfo pipelineCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = (uint32_t)pipelineInfo.shaderStages.size(),
			.pStages = pipelineInfo.shaderStages.data(),
			.pVertexInputState = &pipelineInfo.vertexInputInfo,
			.pInputAssemblyState = &pipelineInfo.inputAssembly,
			.pViewportState = &viewportState,
			.pRasterizationState = &pipelineInfo.rasterizer,
			.pMultisampleState = &pipelineInfo.multisampling,
			.pDepthStencilState = &pipelineInfo.depth,
			.pColorBlendState = &colorBlending,
			.layout = pipelineInfo.pipelineLayout,
			.renderPass = pipelineInfo.pass,
			.subpass = 0,
		};

		//its easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
		VkPipeline pipeline;
		if (vkCreateGraphicsPipelines(
			pipelineInfo.device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
			Logger::logError("Failed to create pipeline");
			return VK_NULL_HANDLE;
		}
		else
		{
			Logger::logMessageFormatted("Created pipeline %u!", pipeline);
			return pipeline;
		}
	}

	void destroyPipeline(VkDevice device, VkPipeline pipeline)
	{
		vkDestroyPipeline(device, pipeline, nullptr);
		Logger::logMessageFormatted("Destroyed pipeline %u!", pipeline);
	}

	VkPipelineLayout createPipelineLayout(

		VkDevice device,
		const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts,
		const std::vector<VkPushConstantRange> &pushConstantRanges)
	{
		VkPipelineLayout pipelineLayout = {};

		const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
			.pSetLayouts = descriptorSetLayouts.data(),
			.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
			.pPushConstantRanges = pushConstantRanges.data()
		};

		vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout);
		Logger::logMessageFormatted("Created pipeline layout %u! ", pipelineLayout);

		return pipelineLayout;
	}

	void destroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout)
	{
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		Logger::logMessageFormatted("Destroyed pipeline layout %u! ", pipelineLayout);
	}

	VkRenderPass createRenderPass(VkDevice device, const std::vector<VkAttachmentDescription> &colorDescriptions, std::optional<VkAttachmentDescription> depthDescription, std::optional<size_t> colorResolveAttachmentIndex)
	{
		assert(colorDescriptions.size() > 0);
		std::vector<VkAttachmentReference> colorReferences = {};
		for (size_t i = 0; i < colorDescriptions.size(); i++)
		{
			if (colorResolveAttachmentIndex.has_value() && colorResolveAttachmentIndex.value() == i) continue;
			colorReferences.push_back(
				VkAttachmentReference
				{
					.attachment = static_cast<uint32_t>(i),
					.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				}
			);
		}

		const VkAttachmentReference depthReference
		{
			.attachment = static_cast<uint32_t>(colorDescriptions.size()),
			.layout = (depthDescription.has_value()) ? depthDescription.value().finalLayout : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
		};

		const VkAttachmentReference colorResolveAttachmentReference
		{
			.attachment = static_cast<uint32_t>(colorResolveAttachmentIndex.value_or(0)),
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		};

		const VkSubpassDescription subpass
		{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size()),
			.pColorAttachments = colorReferences.data(),
			.pResolveAttachments = colorResolveAttachmentIndex.has_value() ? &colorResolveAttachmentReference : nullptr,
			.pDepthStencilAttachment = depthDescription.has_value() ? &depthReference : nullptr,
		};

		const VkSubpassDependency dependency
		{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		};

		std::vector<VkAttachmentDescription> descriptions = std::vector<VkAttachmentDescription>(colorDescriptions);
		if (depthDescription.has_value()) descriptions.push_back(depthDescription.value());

		const VkRenderPassCreateInfo renderPassInfo
		{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = static_cast<uint32_t>(descriptions.size()),
			.pAttachments = descriptions.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = 1,
			.pDependencies = &dependency
		};

		VkRenderPass renderPass = {};
		VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
		Logger::logMessageFormatted("Created render pass %u! ", renderPass);

		return renderPass;
	}

	void destroyRenderPass(VkDevice device, VkRenderPass renderPass)
	{
		vkDestroyRenderPass(device, renderPass, nullptr);
		Logger::logMessageFormatted("Destroyed render pass %u! ", renderPass);
	}


	VkImageView createImageView(
		VkDevice device,
		VkImage image,
		VkFormat format,
		VkImageAspectFlags aspectFlags,
		uint32_t mipLevels)
	{
		const VkImageViewCreateInfo viewInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.subresourceRange = {
				.aspectMask = aspectFlags,
				.baseMipLevel = 0,
				.levelCount = mipLevels,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		VkImageView returnImageView = {};
		VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &returnImageView));
		Logger::logMessageFormatted("Created image view %u! ", returnImageView);

		return returnImageView;
	}

	void destroyImageView(VkDevice device, VkImageView view)
	{
		vkDestroyImageView(device, view, nullptr);
		Logger::logMessageFormatted("Destroyed image view %u! ", view);
	}
}