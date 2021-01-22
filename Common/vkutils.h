#ifndef VK_UTILS_H_DEFINED
#define VK_UTILS_H_DEFINED

#include "vulkan/vulkan.h"
#include <vector>
#include <utility>
#include <optional>
#include <assert.h>
#include "vec.h"
#include "Logger/Logger.h"
#include "AttributeType.h"
#include "CommonConcepts.h"

namespace details
{
	//from: Sascha Willems https://github.com/SaschaWillems/Vulkan/blob/334692a6efbf68039f8838d9bb941bd3234eb152/base/VulkanTools.cpp#L28
	inline const char *errorString(VkResult errorCode)
	{
		switch (errorCode)
		{
#define STR(r) case VK_ ##r: return #r
			STR(NOT_READY);
			STR(TIMEOUT);
			STR(EVENT_SET);
			STR(EVENT_RESET);
			STR(INCOMPLETE);
			STR(ERROR_OUT_OF_HOST_MEMORY);
			STR(ERROR_OUT_OF_DEVICE_MEMORY);
			STR(ERROR_INITIALIZATION_FAILED);
			STR(ERROR_DEVICE_LOST);
			STR(ERROR_MEMORY_MAP_FAILED);
			STR(ERROR_LAYER_NOT_PRESENT);
			STR(ERROR_EXTENSION_NOT_PRESENT);
			STR(ERROR_FEATURE_NOT_PRESENT);
			STR(ERROR_INCOMPATIBLE_DRIVER);
			STR(ERROR_TOO_MANY_OBJECTS);
			STR(ERROR_FORMAT_NOT_SUPPORTED);
			STR(ERROR_SURFACE_LOST_KHR);
			STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
			STR(SUBOPTIMAL_KHR);
			STR(ERROR_OUT_OF_DATE_KHR);
			STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
			STR(ERROR_VALIDATION_FAILED_EXT);
			STR(ERROR_INVALID_SHADER_NV);
#undef STR
		default:
			return "UNKNOWN_ERROR";
		}
	}
}

#define VK_CHECK(vkOp) 				  																			\
{									  																			\
	VkResult res = vkOp; 		  																				\
	if(res != VK_SUCCESS)				  																		\
	{ 								  																			\
		Logger::logErrorFormatted("Vulkan result was not VK_SUCCESS! Error: %s", details::errorString(res));	\
		assert(false);																							\
	}															  												\
}

namespace vkut {

	//structs
	struct Image
	{
		VkImage image;
		VkDeviceMemory memory;
	};

	struct Buffer
	{
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkDeviceSize size;
	};

	[[nodiscard]]
	VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
	void destroyImageView(VkDevice device, VkImageView view);

	[[nodiscard]]
	VkRenderPass createRenderPass(VkDevice device, const std::vector<VkAttachmentDescription> &colorDescriptions, std::optional<VkAttachmentDescription> depthDescription = {}, std::optional<size_t> resolveAttachment = {});
	void destroyRenderPass(VkDevice device, VkRenderPass renderPass);

	struct PipelineInfo
	{
		VkDevice device;
		VkRenderPass pass;
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
		VkPipelineVertexInputStateCreateInfo vertexInputInfo;
		VkPipelineInputAssemblyStateCreateInfo inputAssembly;
		VkViewport viewport;
		VkRect2D scissor;
		VkPipelineRasterizationStateCreateInfo rasterizer;
		VkPipelineColorBlendAttachmentState colorBlendAttachment;
		VkPipelineDepthStencilStateCreateInfo depth;
		VkPipelineMultisampleStateCreateInfo multisampling;
		VkPipelineLayout pipelineLayout;
	};

	VkPipeline createPipeline(const PipelineInfo& pipelineInfo);
	void destroyPipeline(VkDevice device, VkPipeline pipeline);

	[[nodiscard]]
	VkPipelineLayout createPipelineLayout(VkDevice device, const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts, const std::vector<VkPushConstantRange> &pushConstantRanges);
	void destroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout);

	VkSampleCountFlagBits getMaxImageSamples(VkPhysicalDevice physicalDevice);
	VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

	bool hasStencilComponent(VkFormat format);

	[[nodiscard]]
	VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding> &bindings);
	void destroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout);

	[[nodiscard]]
	VkDescriptorPool createDescriptorPool(VkDevice device, const std::vector<VkDescriptorType> &descriptorTypes, uint32_t maxDescriptorCount, uint32_t maxSets);

	//implicitly destroys all descriptor sets created from this pool
	void destroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool);

	struct DescriptorSetInfo
	{
		const void *pNext;
		const uint32_t dstBinding;
		const uint32_t dstArrayElement;
		const uint32_t descriptorCount;
		const VkDescriptorType descriptorType;
		const VkDescriptorImageInfo *pImageInfo;
		const VkDescriptorBufferInfo *pBufferInfo;
		const VkBufferView *pTexelBufferView;
	};
	[[nodiscard]]
	//descriptor sets are implicitly destroyed during destroyDescriptorPool
	VkDescriptorSet createDescriptorSet(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorPool descriptorPool, const std::vector<DescriptorSetInfo> &descriptorWrites);


	struct CreateRenderPassFramebufferInfo
	{
		VkDevice device;
		VkRenderPass renderPass;
		uint32_t width;
		uint32_t height; 
		const std::vector<VkImageView> &colorViews;
		std::optional<VkImageView> depthAttachment;
	};
	[[nodiscard]]
	VkFramebuffer createRenderPassFramebuffer(const CreateRenderPassFramebufferInfo& info);
	void destroyFramebuffer(VkDevice device, VkFramebuffer framebuffer);

	[[nodiscard]]
	std::optional<VkShaderModule> createShaderModule(VkDevice device, const char *path);
	void destroyShaderModule(VkDevice device, VkShaderModule shaderModule);

	[[nodiscard]]
	size_t padUniformBufferSize(size_t originalSize, const VkPhysicalDeviceProperties& deviceProperties);

	struct UploadContext {
		VkDevice device;
		VkFence uploadFence;
		VkCommandPool commandPool;
		VkQueue queue;
	};
	template<con::InvocableWith<VkCommandBuffer> Function_t>
	void submitCommand(UploadContext context, Function_t &&function)
	{
		const VkDevice device = context.device;

		const VkCommandBufferAllocateInfo cmdAllocInfo =
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = context.commandPool,
			.commandBufferCount = 1
		};
		VkCommandBuffer cmd;
		VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd));

		const VkCommandBufferBeginInfo cmdBeginInfo =
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // We will use this command buffer exactly once
		};

		VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

		//execute the function
		function(cmd);

		VK_CHECK(vkEndCommandBuffer(cmd));

		const VkSubmitInfo submit =
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmd
		};

		VK_CHECK(vkQueueSubmit(context.queue, 1, &submit, context.uploadFence)); // uploadFence will now block until the graphic commands finish execution

		vkWaitForFences(device, 1, &context.uploadFence, true, 9999999999);
		vkResetFences(device, 1, &context.uploadFence);

		//this will free the command buffer too
		vkResetCommandPool(device, context.commandPool, 0);
	}
}

#endif