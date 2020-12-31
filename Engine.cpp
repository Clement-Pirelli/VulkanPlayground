#include "Engine.h"
#define VMA_IMPLEMENTATION
#include "VMA/vk_mem_alloc.h"
#include "VkBootstrap.h"
#include "glfw/glfw3.h"
#include <optional>
#include "Logger/Logger.h"
#include "OFileSerialization.h"
#include <array>

constexpr float degToRad = 3.14f / 180.0f;
constexpr float radToDeg = 180.0f / 3.14f;

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

    std::optional<Engine::SwapchainInfo> createSwapchainInfo(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface)
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
            Engine::SwapchainInfo
            {
                .swapchain = vkbSwapchain.swapchain,
                .format = vkbSwapchain.image_format,
                .images = vkbSwapchain.get_images().value(),
                .imageViews = vkbSwapchain.get_image_views().value()
            }
        };
    }
}

void Engine::run()
{
    Time endTime = Time::now();

    float accTime = .0f;

    do 
    {
        glfwPollEvents();
        Time deltaTime = Time::now() - endTime;
        draw(deltaTime);
        endTime = Time::now();

        accTime += deltaTime.asWholeSeconds();
        if(accTime >= 1.0f)
        {
            Logger::logTrivialFormatted("One second has passed! Time is now %f!", Time::now());
            accTime = .0f;
        }

    } while (!glfwWindowShouldClose(window));
}

Engine::Engine()
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
    loadMeshes();
    initPipeline();

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
    VK_CHECK(vkWaitForFences(device, 1, &renderFence, waitAll, oneSecondTimeoutInNS));
    VK_CHECK(vkResetFences(device, 1, &renderFence));

    //note we give presentSemaphore to the swapchain, it'll be signaled when the swapchain is ready to give the next image
    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(device, swapchainInfo.swapchain, oneSecondTimeoutInNS, presentSemaphore, nullptr, &swapchainImageIndex));

    //begin recording the command buffer after resetting it safely (it isn't being used anymore since we acquired the next image)
    VK_CHECK(vkResetCommandBuffer(mainCommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBeginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //we rerecord every frame, so this is a one time submit
    };

    VK_CHECK(vkBeginCommandBuffer(mainCommandBuffer, &commandBufferBeginInfo));
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

        vkCmdBeginRenderPass(mainCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            const vec3 camPos = { 0.f,0.f,-2.0f };
            const mat4x4 viewMatrix = mat4x4::translate(camPos);
            const mat4x4::PerspectiveProjection perspectiveProjection
            {
                .fovX = 70.0f * degToRad,
                .aspectRatio = (float)(windowExtent.width / windowExtent.height),
                .zfar = 200.0f,
                .znear = .01f,
            };

            mat4x4 projectionMatrix = mat4x4::perspective(perspectiveProjection);
            projectionMatrix.at(1,1) *= -1.0f;

            const mat4x4 modelMatrix = mat4x4::rotatedY(Time::now().asSeconds());
            const mat4x4 meshMatrix = projectionMatrix * viewMatrix * modelMatrix;

            const MeshPushConstants constants
            {
                .renderMatrix = meshMatrix
            };

            vkCmdPushConstants(mainCommandBuffer, trianglePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

            vkCmdBindPipeline(mainCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(mainCommandBuffer, 0, 1, &dragonMesh.vertexBuffer.buffer, &offset);
            vkCmdBindIndexBuffer(mainCommandBuffer, dragonMesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(mainCommandBuffer, (uint32_t)dragonMesh.data.indices().size(), 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(mainCommandBuffer);
    }
    VK_CHECK(vkEndCommandBuffer(mainCommandBuffer));
    

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit 
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presentSemaphore, //we wait on the presentSemaphore so the swapchain is ready
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &mainCommandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderSemaphore, //we lock the renderSemaphore for the duration of rendering
    };

    //commands will be executed, renderFence will block until the commands on the graphicsQueue finish execution
    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submit, renderFence));


    VkPresentInfoKHR presentInfo 
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderSemaphore, //wait on rendering to be done before we present
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
    VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));
    QUEUE_DESTROY(vkDestroyCommandPool(device, commandPool, nullptr));

    //allocate the default command buffer that we will use for rendering
    const VkCommandBufferAllocateInfo commandBufferAllocationInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocationInfo, &mainCommandBuffer));
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

    VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &renderFence));
    QUEUE_DESTROY(vkDestroyFence(device, renderFence, nullptr));

    const VkSemaphoreCreateInfo semaphoreCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        //no flags
    };
    VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &presentSemaphore));
    QUEUE_DESTROY(vkDestroySemaphore(device, presentSemaphore, nullptr));
    VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderSemaphore));
    QUEUE_DESTROY(vkDestroySemaphore(device, renderSemaphore, nullptr));
}

void Engine::initPipeline()
{
    std::optional<VkShaderModule> colorVertexModule = vkut::createShaderModule(device, "_assets/shaders/tri_mesh.vert.spv");
    std::optional<VkShaderModule> colorFragmentModule = vkut::createShaderModule(device, "_assets/shaders/shader.frag.spv");

    if(!colorVertexModule.has_value())
    {
        Logger::logError("Could not load vertex module!");
        return;
    }

    if(!colorFragmentModule.has_value())
    {
        vkut::destroyShaderModule(device, colorVertexModule.value());
        Logger::logError("Could not load fragment module!");
        return;
    }


    const VkPushConstantRange meshConstants
    {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(MeshPushConstants),
    };
    trianglePipelineLayout = vkut::createPipelineLayout(device, {}, { meshConstants });
    QUEUE_DESTROY(vkut::destroyPipelineLayout(device, trianglePipelineLayout));

    const VertexInputDescription vertexInputDescription = dragonMesh.getDescription();

    vkut::PipelineInfo pipelineInfo
    {
        .device = device,
        .pass = renderPass,
        .shaderStages
        {
            vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, colorVertexModule.value()),
            vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, colorFragmentModule.value())
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
        .pipelineLayout = trianglePipelineLayout,
    };

    meshPipeline = vkut::createPipeline(pipelineInfo);
    QUEUE_DESTROY(vkut::destroyPipeline(device, meshPipeline));

    vkut::destroyShaderModule(device, colorVertexModule.value());
    vkut::destroyShaderModule(device, colorFragmentModule.value());
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

void Engine::loadMeshes()
{
    constexpr const char *path = "_assets/models/suzanne.o";
    auto loadResult = Mesh::load(path);
    if(loadResult.has_value())
    {
        dragonMesh = std::move(loadResult.value());
    } else 
    {
        Logger::logErrorFormatted("Couldn't load mesh at path %s!", path);
    }
    uploadMesh(dragonMesh);
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
