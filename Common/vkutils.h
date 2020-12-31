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

#define VK_CHECK(vkOp) 				  													\
{									  													\
	VkResult res = vkOp; 		  														\
	if(res != VK_SUCCESS)				  												\
	{ 								  													\
		Logger::logErrorFormatted("Vulkan result was not VK_SUCCESS! Code : %u", res);	\
		assert(false);																	\
	}															  						\
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
	Image createImage(VkDevice device, VkPhysicalDevice physicalDevice, const VkImageCreateInfo &imageCreateInfo, VkMemoryPropertyFlags properties);
	void destroyImage(VkDevice device, Image image);

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
}

#endif