#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <Logger/Logger.h>
#include "VkInitializers.h"

std::optional<AllocatedImage> vkut::loadImageFromFile(ImageLoadContext context, const char *filePath)
{
	int texWidth, texHeight, texChannels;

	stbi_uc *pixels = stbi_load(filePath, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels) {
		return std::nullopt;
	}

	const size_t pixelNumber = texWidth * texHeight;
	const VkDeviceSize imageSize = pixelNumber * 4U;

	const VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB; //this matches exactly with the pixels loaded from stb_image lib

	//allocate temporary buffer for holding texture data to upload
	AllocatedBuffer stagingBuffer = vkmem::createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, context.allocator, VMA_MEMORY_USAGE_CPU_ONLY);
	vkmem::uploadToBuffer<stbi_uc>({ .data = pixels, .buffer = stagingBuffer, .size = imageSize });
	
	stbi_image_free(pixels); //pixel data is now in the staging buffer
	pixels = nullptr;

	const VkExtent3D imageExtent
	{
		.width = static_cast<uint32_t>(texWidth),
		.height = static_cast<uint32_t>(texHeight),
		.depth = 1
	};

	const VkImageCreateInfo imageCreateInfo = vkinit::imageCreateInfo(imageFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);

	const VmaAllocationCreateInfo imageAllocationInfo
	{ 
		.usage = VMA_MEMORY_USAGE_GPU_ONLY
	};

	AllocatedImage newImage;
	VK_CHECK(vkmem::createImage(context.allocator, imageCreateInfo, imageAllocationInfo, newImage, nullptr));

	const vkut::UploadContext uploadContext
	{
		.device = context.device,
		.uploadFence = context.uploadFence,
		.commandPool = context.uploadCommandPool,
		.queue = context.queue
	};

	vkut::submitCommand(uploadContext, [=](VkCommandBuffer cmd) 
	{
		const VkImageSubresourceRange range
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			//base MipLevel and ArrayLayer are 0
			.levelCount = 1,
			.layerCount = 1
		};

		const VkImageMemoryBarrier imageBarrierToTransfer
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = newImage.image,
			.subresourceRange = range,
		};

		//barrier the image into the transfer-receive layout
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrierToTransfer);

		const VkBufferImageCopy copyRegion 
		{
			//buffer Offset, RowLength and ImageHeight are 0
			.imageSubresource = 
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				//mipLevel is 0
				//base array layer is 0
				.layerCount = 1,
			},
			.imageExtent = imageExtent
		};

		vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		const VkImageMemoryBarrier imageBarrierToReadable
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.image = imageBarrierToTransfer.image,
			.subresourceRange = imageBarrierToTransfer.subresourceRange,
		};

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrierToReadable); //image will be in the shader readable layout
	});

	vkmem::destroyBuffer(context.allocator, stagingBuffer);

    return newImage;
}
