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
#include <Timer.h>
#include <unordered_map>
#include <Camera.h>
#include <array>

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

struct SwapchainInfo
{
	VkSwapchainKHR swapchain{};
	VkFormat format{};
	std::vector<VkImage> images{};
	std::vector<VkImageView> imageViews{};
};

struct GPUCameraData {
	mat4x4 view;
	mat4x4 projection;
	mat4x4 viewProjection;
};

struct GPUSceneData {
	 vec4 fogColor; // w is for exponent
	 vec4 fogDistances; //x for min, y for max, zw unused.
	 vec4 ambientColor;
	 vec4 sunlightDirection; //w for sun power
	 vec4 sunlightColor;
};

struct FrameData 
{
	VkSemaphore presentSemaphore;
	VkSemaphore renderSemaphore;
	VkFence renderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	VkDescriptorSet globalDescriptorSet;
};
constexpr uint32_t overlappingFrameNumber = 2;

class Engine
{
public:

	Camera camera;

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

	GLFWwindow *getWindow() const;
	
	void addRenderObject(MeshHandle mesh, MaterialHandle material, mat4x4 transform);

	bool shouldQuit() const;
	
	void draw(Time deltaTime);
	void drawObjects(VkCommandBuffer cmd, RenderObject *first, size_t count);

	Engine(Camera givenCamera, VkExtent2D givenWindowExtent);
	~Engine();
private:

	std::array<FrameData, overlappingFrameNumber> frames;
	FrameData &currentFrame() { return frames[frameCount % overlappingFrameNumber]; }

	void initVulkan();
	void initCommands();
	void initDefaultRenderpass();
	void initFramebuffers();
	void initSyncPrimitives();
	void initDepthResources();
	void initDescriptors();

	bool initialized = false;
	size_t frameCount{};

	VkInstance instance{};
#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debugMessenger{};
#endif
	GLFWwindow *window{};
	VkSurfaceKHR surface{};
	VkPhysicalDevice physicalDevice{};
	VkPhysicalDeviceProperties physicalDeviceProperties{};
	VkDevice device{};


	VkQueue graphicsQueue{};
	uint32_t graphicsQueueFamily{};

	SwapchainInfo swapchainInfo{};

	struct MeshPushConstants {
		vec4 data;
		mat4x4 renderMatrix;
	};

	VkExtent2D windowExtent{};

	VkRenderPass renderPass{};

	std::vector<VkFramebuffer> framebuffers;

	VkImageView depthImageView{};
	AllocatedImage depthImage{};
	VkFormat depthFormat{};

	VkDescriptorSetLayout globalSetLayout{};
	VkDescriptorPool descriptorPool{};

	DeletionQueue mainDeletionQueue{};

	std::vector<RenderObject> renderables;
	std::unordered_map<MaterialHandle, Material> materials;
	std::unordered_map<MeshHandle, Mesh> meshes;

	VmaAllocator allocator{};

	void uploadMesh(Mesh &mesh);
};