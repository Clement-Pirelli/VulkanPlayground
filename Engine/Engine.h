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
#include <TypesafeHandle.h>

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
	VkDescriptorSet textureSet{ VK_NULL_HANDLE };
	VkPipeline pipeline{};
	VkPipelineLayout pipelineLayout{};
};



using MeshHandle = TypesafeHandle<struct MeshID>;
using MaterialHandle = TypesafeHandle<struct MaterialID>;
using TextureHandle = TypesafeHandle<struct TextureID>;

struct RenderObject
{
	Mesh* mesh;
	Material* material;
	mat4x4 transform;
	vec4 color;
};

struct SwapchainInfo
{
	VkSwapchainKHR swapchain{};
	VkFormat format{};
	std::vector<VkImage> images{};
	std::vector<VkImageView> imageViews{};
};

struct GPUCameraData 
{
	mat4x4 view;
	mat4x4 projection;
	mat4x4 viewProjection;
};

struct GPUSceneData 
{
	 vec4 example1;
	 vec4 example2;
	 vec4 ambientColor;
	 vec4 sunlightDirection; //w for sun power
	 vec4 sunlightColor;
};

constexpr size_t maxObjects = 10'000;
struct GPUObjectData 
{
	mat4x4 modelMatrix;
	vec4 color;
};

struct FrameData 
{
	VkSemaphore presentSemaphore;
	VkSemaphore renderSemaphore;
	VkFence renderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	AllocatedBuffer objectsBuffer;
	VkDescriptorSet objectsDescriptor;
};

constexpr uint32_t overlappingFrameNumber = 2;

class Engine
{
public:

	Camera camera;

	[[nodiscard]]
	MaterialHandle loadMaterial(const char *vertexModuleName, const char *fragmentModuleName, MeshHandle vertexDescriptionMesh, TextureHandle texture);
	[[nodiscard]]
	MaterialHandle createMaterial(VkPipeline pipeline, VkPipelineLayout layout, TextureHandle textureHandle);
	[[nodiscard]]
	MeshHandle loadMesh(const char *name);
	[[nodiscard]]
	TextureHandle loadTexture(const char *name);

	[[nodiscard]]
	Material* getMaterial(MaterialHandle handle);
	[[nodiscard]]
	Mesh *getMesh(MeshHandle handle);

	GLFWwindow *getWindow() const;
	
	void addRenderObject(MeshHandle mesh, MaterialHandle material, mat4x4 transform, vec4 color);

	bool shouldQuit() const;
	
	void draw(Time deltaTime);
	void drawObjects(VkCommandBuffer cmd, RenderObject *first, size_t count);

	Engine(Camera givenCamera, VkExtent2D givenWindowExtent);
	~Engine();
private:

	std::array<FrameData, overlappingFrameNumber> frames;
	size_t currentFrameIndex() { return frameCount % overlappingFrameNumber; }
	FrameData &currentFrame() { return frames[currentFrameIndex()]; }

	void initVulkan();
	void initCommands();
	void initDefaultRenderpass();
	void initFramebuffers();
	void initSyncPrimitives();
	void initDepthResources();
	void initDescriptors();
	void initSamplers();

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
	VkDescriptorSetLayout objectsSetLayout{};
	VkDescriptorPool descriptorPool{};

	GPUSceneData sceneParameters{};
	AllocatedBuffer globalBuffer;
	size_t globalBufferStride() const { return vkut::padUniformBufferSize(sizeof(GPUSceneData), physicalDeviceProperties) + vkut::padUniformBufferSize(sizeof(GPUCameraData), physicalDeviceProperties); }
	uint32_t sceneDataOffset(size_t index) const { return cameraDataOffset(index) + static_cast<uint32_t>(vkut::padUniformBufferSize(sizeof(GPUCameraData), physicalDeviceProperties)); }
	uint32_t cameraDataOffset(size_t index) const { return static_cast<uint32_t>(globalBufferStride() * index); }
	VkDescriptorSet globalDescriptorSet;
	VkDescriptorSetLayout singleTextureSetLayout;

	DeletionQueue mainDeletionQueue{};

	std::vector<RenderObject> renderables;
	std::unordered_map<MaterialHandle, Material> materials;
	std::unordered_map<MeshHandle, Mesh> meshes; 
	std::unordered_map<TextureHandle, Texture> textures;
	//todo: make this an unordered map
	VkSampler blockySampler;

	VmaAllocator allocator{};

	VkFence uploadFence;
	VkCommandPool uploadCommandPool;

	void uploadMesh(Mesh &mesh);
};