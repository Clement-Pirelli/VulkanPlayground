#include "Engine.h"
#define VMA_IMPLEMENTATION
#include "VMA/vk_mem_alloc.h"
#include "VkBootstrap.h"
#include "glfw/glfw3.h"
#include <optional>
#include "Logger/Logger.h"
#include "OFileSerialization.h"
#include <array>
#include <MathUtils.h>


#define VKB_CHECK(result, msg) if(!result) {Logger::logErrorFormatted("%s. Cause: %s", msg, result.error().message().c_str()); return; }

#define VK_CHECK(vkOp) 				  													\
{									  													\
	VkResult res = vkOp; 		  														\
	if(res != VK_SUCCESS)				  												\
	{ 								  													\
		Logger::logErrorFormatted("Vulkan result was not VK_SUCCESS! Code : %u", res);	\
		assert(false);																	\
	}															  						\
}

#define QUEUE_DESTROY(expr) { mainDeletionQueue.push([=]() { expr; }); }

namespace
{
    GLFWwindow *createWindow(const char *title, int width, int height)
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow *window = glfwCreateWindow(width, height, title, NULL, NULL);
        assert(window != NULL);
        return window;
    }

    void destroyWindow(GLFWwindow *window)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    VkSurfaceKHR createSurface(VkInstance instance, GLFWwindow *window)
    {
        VkSurfaceKHR result;
        glfwCreateWindowSurface(instance, window, nullptr, &result);
        return result;
    }

    void destroySurface(VkInstance instance, VkSurfaceKHR surface)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }

    std::optional<SwapchainInfo> createSwapchainInfo(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface)
    {
        vkb::SwapchainBuilder swapchainBuilder(physicalDevice, device, surface);
        auto swapchainResult = swapchainBuilder
            .set_desired_present_mode(VkPresentModeKHR::VK_PRESENT_MODE_FIFO_KHR)
            .build();

        if (!swapchainResult) {
            Logger::logErrorFormatted("Failed to create swapchain. Cause %s\n",
                swapchainResult.error().message().c_str());
            return std::nullopt;
        }
        vkb::Swapchain vkbSwapchain = swapchainResult.value();

        return 
        {
            SwapchainInfo
            {
                .swapchain = vkbSwapchain.swapchain,
                .format = vkbSwapchain.image_format,
                .images = vkbSwapchain.get_images().value(),
                .imageViews = vkbSwapchain.get_image_views().value()
            }
        };
    }
}

MaterialHandle Engine::createMaterial(VkPipeline pipeline, VkPipelineLayout layout)
{
    const MaterialHandle newHandle = MaterialHandle{ MaterialHandle::nextHandle };
    MaterialHandle::nextHandle++;
    materials[newHandle] = Material
    {
        .pipeline = pipeline,
        .pipelineLayout = layout
    };
    return newHandle;
}

Material *Engine::getMaterial(MaterialHandle handle)
{
    if (auto it = materials.find(handle);
        it != materials.end()) 
    {
        return &(*it).second;
    }
    else {
        return nullptr;
    }
}

Mesh *Engine::getMesh(MeshHandle handle)
{
    if (auto it = meshes.find(handle);
        it != meshes.end())
    {
        return &(*it).second;
    }
    else {
        return nullptr;
    }
}

GLFWwindow *Engine::getWindow() const
{
    return window;
}

void Engine::drawObjects(VkCommandBuffer cmd, RenderObject *first, size_t count)
{
    const mat4x4 viewMatrix = camera.calculateViewMatrix();
    
    const mat4x4::PerspectiveProjection perspectiveProjection
    {
        .fovX = math::degToRad(70.0f),
        .aspectRatio = (float)(windowExtent.width / windowExtent.height),
        .zfar = 200.0f,
        .znear = .01f,
    }; 
    mat4x4 projectionMatrix = mat4x4::perspective(perspectiveProjection);
    projectionMatrix.at(1, 1) *= -1.0f;

    Mesh* lastMesh = nullptr;
    Material* lastMaterial = nullptr;
    for (int i = 0; i < count; i++)
    {
        const RenderObject& object = first[i];

        //only bind the pipeline if it doesnt match with the already bound one
        if (object.material != lastMaterial) 
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
            lastMaterial = object.material;
        }


        const mat4x4 modelMatrix = object.transform;
        const mat4x4 meshMatrix = projectionMatrix * viewMatrix * modelMatrix;

        const MeshPushConstants constants
        {
            .renderMatrix = meshMatrix
        };

        //upload the mesh to the gpu via pushconstants
        vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

        //only bind the mesh if its a different one from last bind
        if (object.mesh != lastMesh) {
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->vertexBuffer.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, object.mesh->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            lastMesh = object.mesh;
        }
        const VkDeviceSize offset = 0;
        vkCmdDrawIndexed(cmd, (uint32_t)object.mesh->data.indices().size(), 1, 0, 0, 0);
    }
}

bool Engine::shouldQuit() const
{
    return glfwWindowShouldClose(window);
}

Engine::Engine(Camera givenCamera, VkExtent2D givenWindowExtent) : camera(givenCamera), windowExtent(givenWindowExtent)
{
    initVulkan();

    auto swapChainResult = createSwapchainInfo(physicalDevice, device, surface);
    if (swapChainResult.has_value())
    {
        swapchainInfo = swapChainResult.value();
        QUEUE_DESTROY(vkDestroySwapchainKHR(device, swapchainInfo.swapchain, nullptr));
    }
    initDepthResources();

    initCommands();
    initDefaultRenderpass();
    initFramebuffers();
    initSyncPrimitives();
    Logger::logMessage("Successfully initialized vulkan resources!");

    initialized = true;
}

Engine::~Engine()
{
    if(initialized)
    {
        vkDeviceWaitIdle(device);
        mainDeletionQueue.flush();

        Logger::logMessage("Successfully destroyed vulkan resources!");
    }
}

void Engine::draw(Time deltaTime)
{
    constexpr bool waitAll = true;
    constexpr uint64_t oneSecondTimeoutInNS = 1000000000;
    FrameData &frame = currentFrame();

    VK_CHECK(vkWaitForFences(device, 1, &frame.renderFence, waitAll, oneSecondTimeoutInNS));
    VK_CHECK(vkResetFences(device, 1, &frame.renderFence));

    //note we give presentSemaphore to the swapchain, it'll be signaled when the swapchain is ready to give the next image
    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(device, swapchainInfo.swapchain, oneSecondTimeoutInNS, frame.presentSemaphore, nullptr, &swapchainImageIndex));

    //begin recording the command buffer after resetting it safely (it isn't being used anymore since we acquired the next image)
    VK_CHECK(vkResetCommandBuffer(frame.mainCommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBeginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //we rerecord every frame, so this is a one time submit
    };

    VK_CHECK(vkBeginCommandBuffer(frame.mainCommandBuffer, &commandBufferBeginInfo));
    {
        const float flash = (float)abs(sin(frameCount / 120.f));

        const std::array<VkClearValue, 2> clearValues
        {
            VkClearValue
            {
                .color = { { 0.0f, 0.0f, flash, 1.0f } }
            },
            VkClearValue
            {
                .depthStencil = {.depth = 1.0f }
            }
        };


        //We will use the clear color from above, and the framebuffer of the index the swapchain gave us
        const VkRenderPassBeginInfo renderPassBeginInfo
        {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = renderPass,
            .framebuffer = framebuffers[swapchainImageIndex],
            .renderArea
            {
                .offset = {.x = 0, .y = 0},
                .extent = windowExtent
            },
            .clearValueCount = (uint32_t)clearValues.size(),
            .pClearValues = clearValues.data(),

        };

        vkCmdBeginRenderPass(frame.mainCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            drawObjects(frame.mainCommandBuffer, renderables.data(), renderables.size());
        }
        vkCmdEndRenderPass(frame.mainCommandBuffer);
    }
    VK_CHECK(vkEndCommandBuffer(frame.mainCommandBuffer));
    

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit 
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.presentSemaphore, //we wait on the presentSemaphore so the swapchain is ready
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame.mainCommandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &frame.renderSemaphore, //we lock the renderSemaphore for the duration of rendering
    };

    //commands will be executed, renderFence will block until the commands on the graphicsQueue finish execution
    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submit, frame.renderFence));


    VkPresentInfoKHR presentInfo 
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.renderSemaphore, //wait on rendering to be done before we present
        .swapchainCount = 1,
        .pSwapchains = &swapchainInfo.swapchain,
        .pImageIndices = &swapchainImageIndex
    };

    VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));


    frameCount++;
}

void Engine::initVulkan()
{
    vkb::InstanceBuilder instanceBuilder;

    uint32_t count;
    const char **extensions = glfwGetRequiredInstanceExtensions(&count);
    for (uint32_t i = 0; i < count; i++)
    {
        instanceBuilder.enable_extension(extensions[i]);
    }

    auto instanceResult = instanceBuilder.set_app_name("hello vulkan")
        .set_engine_name("unnamed")
        .require_api_version(1, 2, 0)
#ifndef NDEBUG
        .enable_validation_layers()
        .use_default_debug_messenger()
#endif
        .build();

    VKB_CHECK(instanceResult, "Failed to create Vulkan instance");

    vkb::Instance vkbInstance = instanceResult.value();
    instance = vkbInstance.instance;
    QUEUE_DESTROY(vkDestroyInstance(instance, nullptr));
#ifndef NDEBUG
    debugMessenger = vkbInstance.debug_messenger;
    QUEUE_DESTROY(vkb::destroy_debug_utils_messenger(instance, debugMessenger));
#endif

    window = createWindow("Hello Vulkan", windowExtent.width, windowExtent.height);
    QUEUE_DESTROY(destroyWindow(window));
    surface = createSurface(instance, window);
    QUEUE_DESTROY(destroySurface(instance, surface));

    vkb::PhysicalDeviceSelector physicalDeviceSelector{ vkbInstance };
    auto physicalDeviceResult = physicalDeviceSelector.set_surface(surface)
        .set_minimum_version(1, 2) // require a vulkan 1.2 capable device
        .require_dedicated_transfer_queue()
        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .require_present()
        .select();

    VKB_CHECK(physicalDeviceResult, "Failed to select Vulkan Physical Device");

    vkb::PhysicalDevice vkbPhysicalDevice = physicalDeviceResult.value();
    physicalDevice = vkbPhysicalDevice.physical_device;

    vkb::DeviceBuilder deviceBuilder{ vkbPhysicalDevice };
    auto deviceResult = deviceBuilder.build();
    VKB_CHECK(deviceResult, "Failed to create Vulkan device");
    vkb::Device vkbDevice = deviceResult.value();
    device = vkbDevice.device;
    QUEUE_DESTROY(vkDestroyDevice(device, nullptr));

    auto graphicsQueueResult = vkbDevice.get_queue(vkb::QueueType::graphics);
    VKB_CHECK(graphicsQueueResult, "Failed to get graphics queue");
    graphicsQueue = graphicsQueueResult.value();

    auto graphicsQueueFamilyResult = vkbDevice.get_queue_index(vkb::QueueType::graphics);
    VKB_CHECK(graphicsQueueFamilyResult, "Failed to get graphics queue index");
    graphicsQueueFamily = graphicsQueueFamilyResult.value();

    const VmaAllocatorCreateInfo allocatorInfo
    {
        .physicalDevice = physicalDevice,
        .device = device,
        .instance = instance
    };
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));
    QUEUE_DESTROY(vmaDestroyAllocator(allocator));
}

void Engine::initCommands()
{
    const VkCommandPoolCreateInfo commandPoolInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, //we should be able to reset command buffers
        .queueFamilyIndex = graphicsQueueFamily,
    };
    for (FrameData &frame : frames) 
    {
        VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frame.commandPool));
        QUEUE_DESTROY(vkDestroyCommandPool(device, frame.commandPool, nullptr));

        //allocate the default command buffer that we will use for rendering
        const VkCommandBufferAllocateInfo commandBufferAllocationInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocationInfo, &frame.mainCommandBuffer));
    }
  
}

void Engine::initDefaultRenderpass()
{
    const VkAttachmentDescription colorAttachment
    {
        .format = swapchainInfo.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    const VkAttachmentDescription depthAttachment
    {
        .flags = 0,
        .format = depthFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    renderPass = vkut::createRenderPass(device, {colorAttachment}, depthAttachment);
    QUEUE_DESTROY(vkut::destroyRenderPass(device, renderPass));
}

void Engine::initFramebuffers()
{
    const size_t swapchainImageCount = swapchainInfo.images.size();
    framebuffers.resize(swapchainImageCount);

    for (uint32_t i = 0; i < swapchainImageCount; i++) {
        const vkut::CreateRenderPassFramebufferInfo framebufferInfo
        {
            .device = device,
            .renderPass = renderPass,
            .width = windowExtent.width,
            .height = windowExtent.height,
            .colorViews = { swapchainInfo.imageViews[i] },
            .depthAttachment = depthImageView
        };
        framebuffers[i] = vkut::createRenderPassFramebuffer(framebufferInfo);
        QUEUE_DESTROY(vkut::destroyFramebuffer(device, framebuffers[i]));
        QUEUE_DESTROY(vkut::destroyImageView(device, swapchainInfo.imageViews[i]));
    }
}

void Engine::initSyncPrimitives()
{
    const VkFenceCreateInfo fenceCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT //on creation, will be in signaled state
    };

    const VkSemaphoreCreateInfo semaphoreCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        //no flags
    };

    for (FrameData &frame : frames) 
    {
        VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &frame.renderFence));
        QUEUE_DESTROY(vkDestroyFence(device, frame.renderFence, nullptr));

        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frame.presentSemaphore));
        QUEUE_DESTROY(vkDestroySemaphore(device, frame.presentSemaphore, nullptr));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frame.renderSemaphore));
        QUEUE_DESTROY(vkDestroySemaphore(device, frame.renderSemaphore, nullptr));
    }
}

MaterialHandle Engine::createMaterial(const char *vertexModulePath, const char *fragmentModulePath, MeshHandle vertexDescriptionMesh)
{
    std::optional<VkShaderModule> vertexModule = vkut::createShaderModule(device, vertexModulePath);
    std::optional<VkShaderModule> fragmentModule = vkut::createShaderModule(device, fragmentModulePath);
    if (!vertexModule.has_value())
    {
        Logger::logErrorFormatted("Could not load vertex module at path: %s", vertexModulePath);
        return MaterialHandle::invalidHandle();
    }

    if (!fragmentModule.has_value())
    {
        Logger::logErrorFormatted("Could not load fragment module at path: %s", fragmentModulePath);
        vkut::destroyShaderModule(device, vertexModule.value());
        return MaterialHandle::invalidHandle();
    }

    const VkPushConstantRange meshConstants
    {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(MeshPushConstants),
    };
    const VkPipelineLayout pipelineLayout = vkut::createPipelineLayout(device, {}, { meshConstants });
    QUEUE_DESTROY(vkut::destroyPipelineLayout(device, pipelineLayout));

    const VertexInputDescription vertexInputDescription = meshes[vertexDescriptionMesh].getDescription();

    const vkut::PipelineInfo pipelineInfo
    {
        .device = device,
        .pass = renderPass,
        .shaderStages
        {
            vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexModule.value()),
            vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule.value())
        },
        .vertexInputInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = (uint32_t)vertexInputDescription.bindings.size(),
            .pVertexBindingDescriptions = vertexInputDescription.bindings.data(),
            .vertexAttributeDescriptionCount = (uint32_t)vertexInputDescription.attributes.size(),
            .pVertexAttributeDescriptions = vertexInputDescription.attributes.data(),
        },
        .inputAssembly = vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
        .viewport
        {
            .x = .0f,
            .y = .0f,
            .width = (float)windowExtent.width,
            .height = (float)windowExtent.height,
            .minDepth = .0f,
            .maxDepth = 1.0f
        },
        .scissor
        {
            .offset = {0,0},
            .extent = windowExtent
        },
        .rasterizer = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL),
        .colorBlendAttachment = vkinit::colorBlendAttachmentState(),
        .depth = vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL),
        .multisampling = vkinit::multisamplingCreateInfo(),
        .pipelineLayout = pipelineLayout,
    };

    const VkPipeline pipeline = vkut::createPipeline(pipelineInfo);
    QUEUE_DESTROY(vkut::destroyPipeline(device, pipeline));

    vkut::destroyShaderModule(device, vertexModule.value());
    vkut::destroyShaderModule(device, fragmentModule.value());

    return createMaterial(pipeline, pipelineLayout);
}

void Engine::initDepthResources()
{
    const VkExtent3D depthImageExtent 
    {
        windowExtent.width,
        windowExtent.height,
        1
    };

    depthFormat = VK_FORMAT_D32_SFLOAT;

    const VkImageCreateInfo depthImageInfo = vkinit::imageCreateInfo(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

    const VmaAllocationCreateInfo depthImageAllocInfo
    {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    };

    //allocate and create the image
    vmaCreateImage(allocator, &depthImageInfo, &depthImageAllocInfo, &depthImage.image, &depthImage.allocation, nullptr);
    QUEUE_DESTROY(vmaDestroyImage(allocator, depthImage.image, depthImage.allocation));

    //build a image-view for the depth image to use for rendering
    const VkImageViewCreateInfo depthViewInfo = vkinit::imageviewCreateInfo(depthFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(device, &depthViewInfo, nullptr, &depthImageView));
    QUEUE_DESTROY(vkDestroyImageView(device, depthImageView, nullptr));
}

MeshHandle Engine::loadMesh(const char *path)
{
    auto loadResult = Mesh::load(path);
    if (loadResult.has_value())
    {
        const MeshHandle handle = MeshHandle{ MeshHandle::nextHandle };
        MeshHandle::nextHandle++;

        meshes[handle] = std::move(loadResult.value());
        uploadMesh(meshes[handle]);
        return handle;
    }
    else
    {
        Logger::logErrorFormatted("Couldn't load mesh at path %s!", path);
        return MeshHandle::invalidHandle();
    }
}

void Engine::addRenderObject(MeshHandle mesh, MaterialHandle material, mat4x4 transform)
{
    RenderObject object = 
    {
        .mesh = &meshes[mesh],
        .material = &materials[material],
        .transform = transform
    };

    auto pipelineLowerBound = std::lower_bound(renderables.begin(), renderables.end(), object.material->pipeline, [](const RenderObject &ob, VkPipeline pipeline) { return ob.material->pipeline < pipeline; });
    auto meshStart = std::find_if(pipelineLowerBound, renderables.end(), [&object](const RenderObject& ob)
        {
            return object.material->pipeline == ob.material->pipeline && object.mesh == ob.mesh;
        });

    if(meshStart != renderables.end())
    {
        renderables.insert(meshStart, std::move(object));
    } else 
    {
        renderables.insert(pipelineLowerBound, std::move(object));
    }
}

void Engine::uploadMesh(Mesh &mesh)
{
    const VkBufferCreateInfo vertexBufferInfo
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = mesh.data.vertexAmount() * mesh.data.vertexSize(),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    };

    const VmaAllocationCreateInfo vmaallocInfo
    {
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU //VMA library: data should be writeable by CPU, but also readable by GPU
    };

    VK_CHECK(vmaCreateBuffer(allocator, &vertexBufferInfo, &vmaallocInfo,
        &mesh.vertexBuffer.buffer,
        &mesh.vertexBuffer.allocation,
        nullptr));

    QUEUE_DESTROY(vmaDestroyBuffer(allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation));

    {
        void *data;
        vmaMapMemory(allocator, mesh.vertexBuffer.allocation, &data);

        memcpy(data, mesh.data.vertices().data(), mesh.data.vertexAmount() * mesh.data.vertexSize());

        vmaUnmapMemory(allocator, mesh.vertexBuffer.allocation);
    }

    const uint32_t indexBufferSize = (uint32_t)(mesh.data.indices().size() * sizeof(mesh.data.indices()[0]));
    const VkBufferCreateInfo indexBufferInfo
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = indexBufferSize,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    };

    VK_CHECK(vmaCreateBuffer(allocator, &indexBufferInfo, &vmaallocInfo,
        &mesh.indexBuffer.buffer,
        &mesh.indexBuffer.allocation,
        nullptr));

    QUEUE_DESTROY(vmaDestroyBuffer(allocator, mesh.indexBuffer.buffer, mesh.indexBuffer.allocation));

    {
        void *data;
        vmaMapMemory(allocator, mesh.indexBuffer.allocation, &data);

        memcpy(data, mesh.data.indices().data(), indexBufferSize);

        vmaUnmapMemory(allocator, mesh.indexBuffer.allocation);
    }
}
