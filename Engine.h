#pragma once
#pragma warning (disable: 26812)
#include <vector>
#include "VkTypes.h"
#include "VkInitializers.h"
#include <deque>
#include <functional>
#include <Mesh.h>
#include <mat.h>
#include <vec.h>
#include <Time.h>

struct GLFWwindow;

struct DeletionQueue
{
	void push(std::function<void()> &&function) {
		deletors.push_back(function);
	}

	void flush() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)();
		}

		deletors.clear();
	}
private:
	std::deque<std::function<void()>> deletors;
};

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

	struct MeshPushConstants {
		vec4 data;
		mat4x4 renderMatrix;
	};

	static constexpr VkExtent2D windowExtent{ 1700 , 900 };

	VkRenderPass renderPass{};

	std::vector<VkFramebuffer> framebuffers;

	VkSemaphore presentSemaphore{};
	VkSemaphore renderSemaphore{};
	VkFence renderFence{};

	VkPipelineLayout trianglePipelineLayout{};

	Mesh dragonMesh;
	VkPipeline meshPipeline{};

	VkImageView depthImageView{};
	AllocatedImage depthImage{};
	VkFormat depthFormat{};

	DeletionQueue mainDeletionQueue{};


	Engine();
	~Engine();
private:

	void draw(Time deltaTime);
	void initVulkan();
	void initCommands();
	void initDefaultRenderpass();
	void initFramebuffers();
	void initSyncPrimitives();
	void initPipeline();
	void initDepthResources();

	bool initialized = false;
	size_t frameCount{};

	VmaAllocator allocator{};

	void loadMeshes();
	void uploadMesh(Mesh &mesh);
};