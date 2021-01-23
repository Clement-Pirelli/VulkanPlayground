#include "Engine.h"
#include "VkBootstrap.h"
#define GLFW_INCLUDE_NONE
#include "glfw/glfw3.h"
#include <optional>
#include "Logger/Logger.h"
#include "OFileSerialization.h"
#include <array>
#include <MathUtils.h>
#include <CommonConcepts.h>
#include <MemoryUtils.h>
#include <Image.h>

#define VKB_CHECK(result, msg) if(!result) {Logger::logErrorFormatted("%s. Cause: %s", msg, result.error().message().c_str()); return; }
#define QUEUE_DESTROY(expr) { mainDeletionQueue.push([=]() { expr; }); }

namespace
{
    [[nodiscard]]
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

    [[nodiscard]]
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

    [[nodiscard]]
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

MaterialHandle Engine::createMaterial(VkPipeline pipeline, VkPipelineLayout layout, TextureHandle textureHandle)
{   
    VkDescriptorSet materialSet{VK_NULL_HANDLE};

    if(textureHandle != TextureHandle::invalidHandle())
    {
        const VkDescriptorSetAllocateInfo allocInfo
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &singleTextureSetLayout,
        };
        vkAllocateDescriptorSets(device, &allocInfo, &materialSet);

        //write to the descriptor set so that it points to our empire_diffuse texture
        VkDescriptorImageInfo imageBufferInfo
        {
            .sampler = blockySampler,
            .imageView = textures[textureHandle].imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };

        VkWriteDescriptorSet write = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, materialSet, &imageBufferInfo, 0);

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    const MaterialHandle newHandle = MaterialHandle::getNextHandle();
    materials[newHandle] = Material
    {
        .textureSet = materialSet,
        .pipeline = pipeline,
        .pipelineLayout = layout,
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

void Engine::drawObjects(VkCommandBuffer cmd, RenderObject *first, size_t objectCount)
{
    FrameData &frame = currentFrame();
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

    const GPUCameraData cameraData
    {
        .view = viewMatrix,
        .projection = projectionMatrix,
        .viewProjection = projectionMatrix * viewMatrix
    };

    const uint32_t cameraOffset = cameraDataOffset(currentFrameIndex());
    vkmem::uploadToBuffer<GPUCameraData>({ .data = &cameraData, .buffer = globalBuffer, .offset = cameraOffset});

    //slightly more complex than uploadToGPU, essentially copying the transforms of the objects to the mapped pointer
    void *objectData = vkmem::getMappedData(frame.objectsBuffer);
    for (int i = 0; i < objectCount; i++)
    {
        const RenderObject &object = first[i];
        static_cast<GPUObjectData *>(objectData)[i] = { .modelMatrix = object.transform, .color = object.color };
    }

    Mesh* lastMesh = nullptr;
    Material* lastMaterial = nullptr;
    for (int i = 0; i < objectCount; i++)
    {
        const RenderObject& object = first[i];

        //only bind the pipeline if it doesnt match with the already bound one
        if (object.material != lastMaterial) 
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
            lastMaterial = object.material;

            const uint32_t uniformOffset = static_cast<uint32_t>(sceneDataOffset(currentFrameIndex()));
            const std::array<uint32_t, 2> offsets = {cameraOffset, uniformOffset};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 0, 1, &globalDescriptorSet,static_cast<uint32_t>(offsets.size()), offsets.data());
            
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 1, 1, &frame.objectsDescriptor, 0, nullptr); 
            if (object.material->textureSet != VK_NULL_HANDLE) {
                //texture descriptor
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 2, 1, &object.material->textureSet, 0, nullptr);
            }
        }

        const MeshPushConstants constants
        {
            .renderMatrix = object.transform
        };

        vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

        //only bind the mesh if its a different one from last bind
        if (object.mesh != lastMesh) {
            const VkDeviceSize vertexBufferOffset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->vertexBuffer.buffer, &vertexBufferOffset);
            vkCmdBindIndexBuffer(cmd, object.mesh->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            lastMesh = object.mesh;
        }

        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(object.mesh->data.indices().size()), 1, 0, 0, i); //i is passed as firstInstance for the gl_BaseInstance trick
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
    initDescriptors();
    initSamplers();
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
        const float framed = (frameCount / 120.f);

        sceneParameters.ambientColor = { sin(framed),0,cos(framed),1 };

        const vkmem::UploadInfo<GPUSceneData> sceneUploadInfo
        {
            .data = &sceneParameters,
            .buffer = globalBuffer,
            .offset = sceneDataOffset(currentFrameIndex())
        };
        vkmem::uploadToBuffer(sceneUploadInfo);

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
            .clearValueCount = static_cast<uint32_t>(clearValues.size()),
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

    const auto instanceResult = instanceBuilder.set_app_name("hello vulkan")
        .set_engine_name("unnamed")
        .require_api_version(1, 2, 0)
#ifndef NDEBUG
        .enable_validation_layers()
        .set_debug_callback([](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *)
            -> VkBool32 
            {
                auto severity = vkb::to_string_message_severity(messageSeverity);
                auto type = vkb::to_string_message_type(messageType);
                Logger::logErrorFormatted("[%s : %s] : %s", severity, type, pCallbackData->pMessage);
                return VK_FALSE;
            })
#endif
        .build();

    VKB_CHECK(instanceResult, "Failed to create Vulkan instance");

    const vkb::Instance vkbInstance = instanceResult.value();
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
    const auto physicalDeviceResult = physicalDeviceSelector.set_surface(surface)
        .set_minimum_version(1, 2) // require a vulkan 1.2 capable device
        .require_dedicated_transfer_queue()
        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .require_present()
        .select();

    VKB_CHECK(physicalDeviceResult, "Failed to select Vulkan Physical Device");

    const vkb::PhysicalDevice vkbPhysicalDevice = physicalDeviceResult.value();
    physicalDevice = vkbPhysicalDevice.physical_device;
    
    const vkb::DeviceBuilder deviceBuilder{ vkbPhysicalDevice };
    const auto deviceResult = deviceBuilder.build();
    VKB_CHECK(deviceResult, "Failed to create Vulkan device");
    const vkb::Device vkbDevice = deviceResult.value();
    device = vkbDevice.device;
    QUEUE_DESTROY(vkDestroyDevice(device, nullptr));

    const auto graphicsQueueResult = vkbDevice.get_queue(vkb::QueueType::graphics);
    VKB_CHECK(graphicsQueueResult, "Failed to get graphics queue");
    graphicsQueue = graphicsQueueResult.value();

    const auto graphicsQueueFamilyResult = vkbDevice.get_queue_index(vkb::QueueType::graphics);
    VKB_CHECK(graphicsQueueFamilyResult, "Failed to get graphics queue index");
    graphicsQueueFamily = graphicsQueueFamilyResult.value();

    const VmaAllocatorCreateInfo allocatorInfo
    {
        .physicalDevice = physicalDevice,
        .device = device,
        .instance = instance
    };
    VK_CHECK(vkmem::createAllocator(allocatorInfo, allocator));
    QUEUE_DESTROY(vkmem::destroyAllocator(allocator));

    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
}

void Engine::initCommands()
{
    const VkCommandPoolCreateInfo mainCommandPoolInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, //we should be able to reset command buffers
        .queueFamilyIndex = graphicsQueueFamily,
    };
    for (FrameData &frame : frames) 
    {
        VK_CHECK(vkCreateCommandPool(device, &mainCommandPoolInfo, nullptr, &frame.commandPool));
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

    const VkCommandPoolCreateInfo uploadCommandPoolInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueFamily,
    };
    VK_CHECK(vkCreateCommandPool(device, &uploadCommandPoolInfo, nullptr, &uploadCommandPool));
    QUEUE_DESTROY(vkDestroyCommandPool(device, uploadCommandPool, nullptr););
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
    const VkFenceCreateInfo frameFenceCreateInfo
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
        VK_CHECK(vkCreateFence(device, &frameFenceCreateInfo, nullptr, &frame.renderFence));
        QUEUE_DESTROY(vkDestroyFence(device, frame.renderFence, nullptr));

        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frame.presentSemaphore));
        QUEUE_DESTROY(vkDestroySemaphore(device, frame.presentSemaphore, nullptr));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frame.renderSemaphore));
        QUEUE_DESTROY(vkDestroySemaphore(device, frame.renderSemaphore, nullptr));
    }

    const VkFenceCreateInfo uploadFenceCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateFence(device, &uploadFenceCreateInfo, nullptr, &uploadFence));
    QUEUE_DESTROY(vkDestroyFence(device, uploadFence, nullptr));
}

MaterialHandle Engine::loadMaterial(const char *vertexModulePath, const char *fragmentModulePath, MeshHandle vertexDescriptionMesh, TextureHandle textureHandle)
{
    std::optional<VkShaderModule> vertexModule = vkut::createShaderModule(device, vertexModulePath);
    std::optional<VkShaderModule> fragmentModule = vkut::createShaderModule(device, fragmentModulePath);
    if (!vertexModule.has_value())
    {
        Logger::logErrorFormatted("Could not load vertex module at path \"%s\"", vertexModulePath);
        return MaterialHandle::invalidHandle();
    }

    if (!fragmentModule.has_value())
    {
        Logger::logErrorFormatted("Could not load fragment module at path: \"%s\"", fragmentModulePath);
        vkut::destroyShaderModule(device, vertexModule.value());
        return MaterialHandle::invalidHandle();
    }

    const VkPushConstantRange meshConstants
    {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(MeshPushConstants),
    };
    const VkPipelineLayout pipelineLayout = vkut::createPipelineLayout(device, { globalSetLayout, objectsSetLayout, singleTextureSetLayout }, { meshConstants });
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
            .vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputDescription.bindings.size()),
            .pVertexBindingDescriptions = vertexInputDescription.bindings.data(),
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputDescription.attributes.size()),
            .pVertexAttributeDescriptions = vertexInputDescription.attributes.data(),
        },
        .inputAssembly = vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
        .viewport
        {
            .x = .0f,
            .y = .0f,
            .width = static_cast<float>(windowExtent.width),
            .height = static_cast<float>(windowExtent.height),
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

    Logger::logMessageFormatted("Successfully loaded material with fragment path \"%s\" and vertex path \"%s\"!", fragmentModulePath, vertexModulePath);

    return createMaterial(pipeline, pipelineLayout, textureHandle);
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
    VK_CHECK(vkmem::createImage(allocator, depthImageInfo, depthImageAllocInfo, depthImage, nullptr));
    QUEUE_DESTROY(vkmem::destroyImage(allocator, depthImage));

    //build a image-view for the depth image to use for rendering
    const VkImageViewCreateInfo depthViewInfo = vkinit::imageviewCreateInfo(depthFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(device, &depthViewInfo, nullptr, &depthImageView));
    QUEUE_DESTROY(vkDestroyImageView(device, depthImageView, nullptr));
}

void Engine::initDescriptors()
{
    
    //global set layout
    {
        const VkDescriptorSetLayoutBinding cameraBufferBinding
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
        };

        const VkDescriptorSetLayoutBinding sceneParamBufferBinding
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
        };
        globalSetLayout = vkut::createDescriptorSetLayout(device, { cameraBufferBinding, sceneParamBufferBinding });
        QUEUE_DESTROY(vkut::destroyDescriptorSetLayout(device, globalSetLayout));
    }

    VkDescriptorSetLayoutBinding objectsBinding
    {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
    };

    objectsSetLayout = vkut::createDescriptorSetLayout(device, { objectsBinding });
    QUEUE_DESTROY(vkut::destroyDescriptorSetLayout(device, objectsSetLayout));

    const uint32_t maxDescriptorSets = 10;  //this is an arbitrary big number for now
    const std::vector<VkDescriptorPoolSize> sizes =
    {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 
            .descriptorCount = maxDescriptorSets 
        },
        { 
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 
            .descriptorCount = maxDescriptorSets
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 
            .descriptorCount = maxDescriptorSets
        },
        { 
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
            .descriptorCount = 10 
        }
    };

    const VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = maxDescriptorSets * static_cast<uint32_t>(sizes.size()),
        .poolSizeCount = static_cast<uint32_t>(sizes.size()),
        .pPoolSizes = sizes.data()
    };
   
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
    QUEUE_DESTROY(vkDestroyDescriptorPool(device, descriptorPool, nullptr));

    VkDescriptorSetLayoutBinding textureBinding =
    {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };

    singleTextureSetLayout = vkut::createDescriptorSetLayout(device, { textureBinding });
    QUEUE_DESTROY(vkut::destroyDescriptorSetLayout(device, singleTextureSetLayout));

    //global set allocations
    {
        const size_t globalBufferSize = cameraDataOffset(overlappingFrameNumber+1);
        globalBuffer = vkmem::createBuffer(globalBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, allocator, VMA_MEMORY_USAGE_CPU_TO_GPU);
        QUEUE_DESTROY(vkmem::destroyBuffer(allocator, globalBuffer));

        const VkDescriptorSetAllocateInfo globalAllocationInfo
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &globalSetLayout
        };

        VK_CHECK(vkAllocateDescriptorSets(device, &globalAllocationInfo, &globalDescriptorSet));
        QUEUE_DESTROY(vkFreeDescriptorSets(device, descriptorPool, 1, &globalDescriptorSet));
    }

    for (uint32_t i = 0; i < overlappingFrameNumber; i++)
    {
        FrameData &currentFrame = frames[i];
        
        //objects set allocations
        {
            currentFrame.objectsBuffer = vkmem::createBuffer(sizeof(GPUObjectData) * maxObjects, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, allocator, VMA_MEMORY_USAGE_CPU_TO_GPU);
            QUEUE_DESTROY(vkmem::destroyBuffer(allocator, currentFrame.objectsBuffer));
            const VkDescriptorSetAllocateInfo objectsAllocationInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = &objectsSetLayout
            };

            vkAllocateDescriptorSets(device, &objectsAllocationInfo, &currentFrame.objectsDescriptor);
            QUEUE_DESTROY(vkFreeDescriptorSets(device, descriptorPool, 1, &currentFrame.objectsDescriptor));
        }

        const VkDescriptorBufferInfo cameraBufferInfo
        {
            .buffer = globalBuffer.buffer,
            .offset = 0, //even though we are offsetting into this buffer, it's done dynamically
            .range = sizeof(GPUCameraData)
        };
        const VkWriteDescriptorSet cameraWrite
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = globalDescriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &cameraBufferInfo
        };

        const VkDescriptorBufferInfo sceneInfo
        {
            .buffer = globalBuffer.buffer,
            .offset = 0, //even though we are offsetting into this buffer, it's done dynamically
            .range = sizeof(GPUSceneData)
        };
        const VkWriteDescriptorSet sceneWrite
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = globalDescriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &sceneInfo
        };

        const VkDescriptorBufferInfo objectBufferInfo
        {
            .buffer = currentFrame.objectsBuffer.buffer,
            .offset = 0,
            .range = sizeof(GPUObjectData) * maxObjects
        };
        const VkWriteDescriptorSet objectWrite
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = currentFrame.objectsDescriptor,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &objectBufferInfo
        };

        std::array<VkWriteDescriptorSet, 3> setWrites = { cameraWrite, sceneWrite, objectWrite };

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr); //here we update both the global and the objects descriptor sets at once - this is normal
    }
}

void Engine::initSamplers()
{
    VkSamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(VK_FILTER_NEAREST);
    vkCreateSampler(device, &samplerInfo, nullptr, &blockySampler);
}

MeshHandle Engine::loadMesh(const char *path)
{
    const auto loadResult = Mesh::load(path);
    if (!loadResult.has_value())
    {
        Logger::logErrorFormatted("Failed to load mesh at path \"%s\"!", path);
        return MeshHandle::invalidHandle();
    }

    const MeshHandle handle = MeshHandle::getNextHandle();

    meshes[handle] = loadResult.value();
    uploadMesh(meshes[handle]);
    Logger::logMessageFormatted("Successfully loaded mesh at path \"%s\"!", path);
    return handle;
}

TextureHandle Engine::loadTexture(const char *path)
{
    const vkut::ImageLoadContext loadContext
    {
        .device = device,
        .allocator = allocator,
        .uploadFence = uploadFence,
        .uploadCommandPool = uploadCommandPool,
        .queue = graphicsQueue
    };
    const std::optional<AllocatedImage> image = vkut::loadImageFromFile(loadContext, path);
    if(!image.has_value())
    {
        Logger::logErrorFormatted("Failed to load texture at path \"%s\"!", path);
        return TextureHandle::invalidHandle();
    }
    
    VkImageView view = vkut::createImageView(device, image.value().image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    QUEUE_DESTROY(vkmem::destroyImage(allocator, image.value()));
    QUEUE_DESTROY(vkut::destroyImageView(device, view));

    TextureHandle handle = TextureHandle::getNextHandle();

    const Texture result = { image.value(), view };
    textures[handle] = result;
    Logger::logMessageFormatted("Successfully loaded texture at path \"%s\"!", path);
    return handle;
}

void Engine::addRenderObject(MeshHandle mesh, MaterialHandle material, mat4x4 transform, vec4 color)
{
    auto meshIt = meshes.find(mesh);
    if(meshIt == meshes.end())
    {
        Logger::logErrorFormatted("Could not find mesh for handle %u", static_cast<uint64_t>(mesh));
        return;
    }
    auto materialIt = materials.find(material);
    if(materialIt == materials.end())
    {
        Logger::logErrorFormatted("Could not find material for handle %u", static_cast<uint64_t>(material));
        return;
    }

    const RenderObject object = 
    {
        .mesh = &(*meshIt).second,
        .material = &(*materialIt).second,
        .transform = transform,
        .color = color
    };

    //sorting by pipeline and then by mesh
    auto pipelineLowerBound = std::lower_bound(renderables.begin(), renderables.end(), object.material->pipeline, [](const RenderObject &ob, VkPipeline pipeline) { return ob.material->pipeline < pipeline; });
    auto meshStart = std::find_if(pipelineLowerBound, renderables.end(), [&object](const RenderObject& ob)
        {
            return object.material->pipeline == ob.material->pipeline && object.mesh == ob.mesh;
        });

    if(meshStart != renderables.end())
    {
        renderables.insert(meshStart, object);
    } else 
    {
        renderables.insert(pipelineLowerBound, object);
    }
}

void Engine::uploadMesh(Mesh &mesh)
{
    const VmaMemoryUsage vmaStagingBufferUsage = VMA_MEMORY_USAGE_CPU_ONLY; //on CPU RAM
    const VmaMemoryUsage vmaBuffersUsage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    const vkut::UploadContext uploadContext
    {
        .device = device,
        .uploadFence = uploadFence,
        .commandPool = uploadCommandPool,
        .queue = graphicsQueue,
    };

    //vertex buffer
    {
        const uint32_t vertexBufferSize = static_cast<uint32_t>(mesh.data.vertexAmount() * mesh.data.vertexSize());

        AllocatedBuffer vertexStagingBuffer = vkmem::createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, allocator, vmaStagingBufferUsage);
        memcpy(vkmem::getMappedData(vertexStagingBuffer), mesh.data.vertices().data(), vertexBufferSize);

        const VkBufferUsageFlags vkVertexBufferUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        mesh.vertexBuffer = vkmem::createBuffer(vertexBufferSize, vkVertexBufferUsage, allocator, vmaBuffersUsage);
        QUEUE_DESTROY(vkmem::destroyBuffer(allocator, mesh.vertexBuffer));

        vkut::submitCommand(uploadContext,
            [=](VkCommandBuffer cmd) 
            {
                const VkBufferCopy copy
                {
                    .srcOffset = 0,
                    .dstOffset = 0,
                    .size = vertexBufferSize,
                };
                vkCmdCopyBuffer(cmd, vertexStagingBuffer.buffer, mesh.vertexBuffer.buffer, 1, &copy);
            });

        vkmem::destroyBuffer(allocator, vertexStagingBuffer);
    }
    
    //index buffer
    {
        const uint32_t indexBufferSize = static_cast<uint32_t>(mesh.data.indices().size() * sizeof(mesh.data.indices()[0]));
        
        AllocatedBuffer indexStagingBuffer = vkmem::createBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, allocator, vmaStagingBufferUsage);
        memcpy(vkmem::getMappedData(indexStagingBuffer), mesh.data.indices().data(), indexBufferSize);

        const VkBufferUsageFlags vkIndexBufferUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        mesh.indexBuffer = vkmem::createBuffer(indexBufferSize, vkIndexBufferUsage, allocator, vmaBuffersUsage);
        QUEUE_DESTROY(vkmem::destroyBuffer(allocator, mesh.indexBuffer));

        vkut::submitCommand(uploadContext,
            [=](VkCommandBuffer cmd)
            {
                const VkBufferCopy copy
                {
                    .srcOffset = 0,
                    .dstOffset = 0,
                    .size = indexBufferSize,
                };
                vkCmdCopyBuffer(cmd, indexStagingBuffer.buffer, mesh.indexBuffer.buffer, 1, &copy);
            });

        vkmem::destroyBuffer(allocator, indexStagingBuffer);
    }
}
