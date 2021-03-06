#pragma once
#pragma warning (disable: 26812)
#include "VkTypes.h"
#include "VkInitializers.h"
#include <Mesh.h>
#include <mat.h>
#include <vec.h>
#include <Timer.h>
#include <TypesafeHandle.h>
#include <ResourceMap.h>
#include <ConsoleVariables.h>
#include <DescriptorSetBuilder.h>

#include <deque>
#include <functional>
#include <unordered_map>
#include <array>
#include <vector>

class Camera;
class Window;

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
	VkExtent2D extent{};
	std::vector<VkImage> images{};
	std::vector<VkImageView> imageViews{};
	uint32_t lastAcquiredImageIndex;
};

struct GPUCameraData 
{
	mat4x4 view;
	mat4x4 projection;
	mat4x4 viewProjection;
};

struct GPUSceneData 
{
	 vec4 ambientColor;
	 vec4 sunlightDirection; //w for sun power
	 vec4 sunlightColor;
};

constexpr size_t maxObjects = 10'000; //todo: make this dynamic
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
	
	void addRenderObject(MeshHandle mesh, MaterialHandle material, mat4x4 transform, vec4 color);
	
	void getNextImage(VkSemaphore waitSemaphore);
	void startRecording(VkCommandBuffer cmd, VkFence waitFence);
	void endRecording(FrameData &frame);
	void drawToScreen(Time deltaTime, const Camera& camera);
	void present(VkSemaphore waitSemaphore);
	void drawToBuffer(Time deltaTime, const Camera& camera, std::byte* data, size_t count);
	void drawObjects(VkCommandBuffer cmd, RenderObject *first, size_t count, const Camera& camera);

	Engine(Window& window);
	~Engine();

private:

	std::array<FrameData, overlappingFrameNumber> frames;
	size_t currentFrameIndex() { return frameCount % overlappingFrameNumber; }
	FrameData &currentFrame() { return frames[currentFrameIndex()]; }

	void initVulkan();
	void initImgui();
	void initCommands();
	void initDefaultRenderpass();
	void initFramebuffers(bool recreating = false);
	void initSyncPrimitives();
	void initDepthResources(bool recreating = false);
	void initDescriptors();
	void initSamplers();

	void onWindowResize();

	bool initialized = false;
	size_t frameCount{};

	VkInstance instance{};
#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debugMessenger{};
#endif
	Window &window;
	VkSurfaceKHR surface{};
	VkPhysicalDevice physicalDevice{};
	VkPhysicalDeviceProperties physicalDeviceProperties{};
	VkDevice device{};


	VkQueue graphicsQueue{};
	uint32_t graphicsQueueFamily{};

	SwapchainInfo swapchainInfo{};

	VkExtent2D windowExtent{};

	VkRenderPass renderPass{};
	std::vector<VkFramebuffer> framebuffers;

	VkImageView depthImageView{};
	AllocatedImage depthImage{};
	VkFormat depthFormat{};

	std::unique_ptr<vkut::DescriptorAllocator> descriptorAllocator;
	std::unique_ptr<vkut::DescriptorLayoutCache> descriptorLayoutCache;

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
	ResourceMap<MaterialHandle, Material> materials;
	ResourceMap<MeshHandle, Mesh> meshes;
	ResourceMap<TextureHandle, Texture> textures;
	//todo: make this an unordered map of samplers, create as they're asked
	VkSampler blockySampler;

	vkut::UploadContext getUploadContext() const;

	VmaAllocator allocator{};

	VkFence uploadFence;
	VkCommandPool uploadCommandPool;

	void uploadMesh(Mesh &mesh);
};