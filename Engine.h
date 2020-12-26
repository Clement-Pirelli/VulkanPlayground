#pragma once
#include <vector>
#include "VkTypes.h"
#include "VkInitializers.h"

struct GLFWwindow;

class Engine
{
public:

	void run();

	VkInstance instance{};
#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debugMessenger{};
#endif
	GLFWwindow *window{};
	VkSurfaceKHR surface{};
	VkPhysicalDevice physicalDevice{};
	VkDevice device{};

	VkQueue graphicsQueue{}; 
	uint32_t graphicsQueueFamily{};

	VkCommandPool commandPool{};
	VkCommandBuffer mainCommandBuffer{};

	struct SwapchainInfo
	{
		VkSwapchainKHR swapchain{};
		VkFormat format{};
		std::vector<VkImage> images{};
		std::vector<VkImageView> imageViews{};
	};

	SwapchainInfo swapchainInfo{};

	static constexpr VkExtent2D windowExtent{ 1700 , 900 };

	VkRenderPass renderPass;

	std::vector<VkFramebuffer> framebuffers;

	VkSemaphore presentSemaphore;
	VkSemaphore renderSemaphore;
	VkFence renderFence;

	Engine();
	~Engine();
private:

	void draw();
	void initVulkan();
	void initCommands();
	void initDefaultRenderpass();
	void initFramebuffers();
	void initSyncPrimitives();

	bool initialized = false;
	size_t frameCount{};
};