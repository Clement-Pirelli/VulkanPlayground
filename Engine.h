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
#include <unordered_map>

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

struct Material 
{
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

template<typename ID>
struct EngineResourceHandle
{
	constexpr EngineResourceHandle() = default;
	static constexpr EngineResourceHandle<ID> invalidHandle() { return EngineResourceHandle<ID>{~0U}; };
	bool operator!=(EngineResourceHandle<ID> h) const
	{
		return h.handle != handle;
	}
	bool operator==(EngineResourceHandle<ID> h) const
	{
		return h.handle == handle;
	}

	auto operator<=>(EngineResourceHandle<ID> h) const
	{
		return h.handle <=> handle;
	}
private:

	static uint64_t nextHandle;
	explicit constexpr EngineResourceHandle(uint64_t givenHandle) : handle(givenHandle) {}	
	
	uint64_t handle{};

	friend struct std::hash<EngineResourceHandle<ID>>;
	friend class Engine;
};

template<typename ID>
uint64_t EngineResourceHandle<ID>::nextHandle = { 1 };

namespace std
{
	template<typename ID> 
	struct hash<EngineResourceHandle<ID>>
	{
		size_t operator()(EngineResourceHandle<ID> handle) const
		{
			//handles are unique, so hashing would be a waste of time
			return (size_t)handle.handle;
		}
	};
}
using MeshHandle = EngineResourceHandle<struct MeshID>;
using MaterialHandle = EngineResourceHandle<struct MaterialID>;

struct RenderObject
{
	Mesh* mesh;
	Material* material;
	mat4x4 transform;
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

	VkImageView depthImageView{};
	AllocatedImage depthImage{};
	VkFormat depthFormat{};

	DeletionQueue mainDeletionQueue{};

	std::vector<RenderObject> renderables;
	std::unordered_map<MaterialHandle, Material> materials;
	std::unordered_map<MeshHandle, Mesh> meshes;

	[[nodiscard]]
	MaterialHandle createMaterial(const char *vertexModulePath, const char *fragmentModulePath, MeshHandle vertexDescriptionMesh);
	[[nodiscard]]
	MaterialHandle createMaterial(VkPipeline pipeline, VkPipelineLayout layout);
	[[nodiscard]]
	MeshHandle loadMesh(const char *path);
	
	[[nodiscard]]
	Material* getMaterial(MaterialHandle handle);
	[[nodiscard]]
	Mesh *getMesh(MeshHandle handle);
	
	void addRenderObject(MeshHandle mesh, MaterialHandle material, mat4x4 transform);

	void drawObjects(VkCommandBuffer cmd, RenderObject *first, size_t count);

	Engine();
	~Engine();
private:

	void draw(Time deltaTime);
	void initVulkan();
	void initCommands();
	void initDefaultRenderpass();
	void initFramebuffers();
	void initSyncPrimitives();
	void initDepthResources();

	bool initialized = false;
	size_t frameCount{};

	VmaAllocator allocator{};

	void uploadMesh(Mesh &mesh);
};