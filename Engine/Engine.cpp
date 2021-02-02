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
#include <string>
#include <Camera.h>
#include <Window.h>

//imgui
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>

#define VKB_CHECK(result, msg) if(!result) { Logger::logErrorFormatted("%s. Cause: %s", msg, result.error().message().c_str()); return; }
#define QUEUE_DESTROY(expr) { mainDeletionQueue.push([=]() { expr; }); }
#define QUEUE_DESTROY_REF(expr){ mainDeletionQueue.push([&](){ expr; }); }

namespace
{   
    constexpr const char *assetsFolderPath = "../_assets/"; //TODO: put this stuff in its own file!
    std::string getTexturePath(const char *textureName) { return (std::string(assetsFolderPath) + "textures/") + textureName; }
    std::string getShaderPath(const char *shaderName) { return (std::string(assetsFolderPath) + "shaders/") + shaderName; }
    std::string getModelPath(const char *modelName) { return (std::string(assetsFolderPath) + "models/") + modelName; }

    [[nodiscard]]
    VkSurfaceKHR createSurface(VkInstance instance, Window &window)
    {
        VkSurfaceKHR result;
        glfwCreateWindowSurface(instance, window.get(), nullptr, &result);
        return result;
    }

    void destroySurface(VkInstance instance, VkSurfaceKHR surface)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }

    [[nodiscard]]
    std::optional<SwapchainInfo> createSwapchainInfo(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE)
    {
        vkb::SwapchainBuilder swapchainBuilder(physicalDevice, device, surface);
        swapchainBuilder.set_desired_present_mode(VkPresentModeKHR::VK_PRESENT_MODE_FIFO_KHR);
        if(oldSwapchain != VK_NULL_HANDLE)
        {
            swapchainBuilder.set_old_swapchain(oldSwapchain);
        }

        auto swapchainResult = swapchainBuilder.build();

        if (!swapchainResult) {
            Logger::logErrorFormatted("Failed to create swapchain. Cause %s\n",
                swapchainResult.error().message().c_str());
            return std::nullopt;
        }
        vkb::Swapchain& vkbSwapchain = swapchainResult.value();

        return 
        {
            SwapchainInfo
            {
                .swapchain = vkbSwapchain.swapchain,
                .format = vkbSwapchain.image_format,
                .extent = vkbSwapchain.extent,
                .images = vkbSwapchain.get_images().value(),
                .imageViews = vkbSwapchain.get_image_views().value()
            }
        };
    }
}

MaterialHandle Engine::createMaterial(VkPipeline pipeline, VkPipelineLayout layout, TextureHandle textureHandle)
{   
    VkDescriptorSet materialSet{ VK_NULL_HANDLE };

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

        const VkWriteDescriptorSet write = vkinit::writeDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, materialSet, &imageBufferInfo, 0);

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

void Engine::drawObjects(VkCommandBuffer cmd, RenderObject *first, size_t objectCount, const Camera& camera)
{
    FrameData &frame = currentFrame();
    const mat4x4 viewMatrix = camera.calculateViewMatrix();
    
    const mat4x4::PerspectiveProjection perspectiveProjection
    {
        .fovX = math::degToRad(70.0f),
        .aspectRatio = windowExtent.width / static_cast<float>(windowExtent.height),
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

Engine::Engine(Window& givenWindow) : window(givenWindow)
{
    ivec2 windowSize = window.resolution();
    windowExtent = { .width = (uint32_t)windowSize.x(), .height = (uint32_t)windowSize.y() };

    initVulkan();

    auto swapChainResult = createSwapchainInfo(physicalDevice, device, surface);
    if (swapChainResult.has_value())
    {
        swapchainInfo = swapChainResult.value();
        QUEUE_DESTROY_REF(vkDestroySwapchainKHR(device, swapchainInfo.swapchain, nullptr));
    }
    initDepthResources();

    initCommands();
    initDefaultRenderpass();
    initFramebuffers();
    initSyncPrimitives();
    initDescriptors();
    initSamplers();
    initImgui();
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

void Engine::drawToScreen(Time deltaTime, const Camera& camera)
{
    if(settings.renderUI) ImGui::Render();

    FrameData &frame = currentFrame();
    constexpr bool waitAll = true;
    constexpr uint64_t bigTimeout = 1000000000;

    VK_CHECK(vkWaitForFences(device, 1, &frame.renderFence, waitAll, bigTimeout));
    VK_CHECK(vkResetFences(device, 1, &frame.renderFence));

    //note we give presentSemaphore to the swapchain, it'll be signaled when the swapchain is ready to give the next image
    do {
        const VkResult result = vkAcquireNextImageKHR(device, swapchainInfo.swapchain, bigTimeout, frame.presentSemaphore, nullptr, &swapchainInfo.lastAcquiredImageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            onResize();
            continue; //try again
        }
        if (result != VK_SUBOPTIMAL_KHR)
        {
            VK_CHECK(result);
        }
    } while (false);


    //begin recording the command buffer after resetting it safely (it isn't being used anymore since we acquired the next image)
    VK_CHECK(vkResetCommandBuffer(frame.mainCommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBeginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //we rerecord every frame, so this is a one time submit
    };

    VK_CHECK(vkBeginCommandBuffer(frame.mainCommandBuffer, &commandBufferBeginInfo));
    {
        const VkViewport viewport
        {
            .x = .0f,
            .y = .0f,
            .width = static_cast<float>(windowExtent.width),
            .height = static_cast<float>(windowExtent.height),
            .minDepth = .0f,
            .maxDepth = 1.0f
        };

        const VkRect2D scissor
        {
            .offset = {0,0},
            .extent = windowExtent
        };

        vkCmdSetViewport(frame.mainCommandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(frame.mainCommandBuffer, 0, 1, &scissor);
        

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
            .framebuffer = framebuffers[swapchainInfo.lastAcquiredImageIndex],
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
            drawObjects(frame.mainCommandBuffer, renderables.data(), renderables.size(), camera);
            if(settings.renderUI) ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.mainCommandBuffer);
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
        .pImageIndices = &swapchainInfo.lastAcquiredImageIndex
    };

    //check whether we need to resize
    const VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);
    const ivec2 swapchainExtents{ (int)swapchainInfo.extent.width, (int)swapchainInfo.extent.height };
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR
        || presentResult == VK_SUBOPTIMAL_KHR 
        || window.resolution() != swapchainExtents)
    {
        onResize();
    }
    else
    {
        VK_CHECK(presentResult);
    }


    frameCount++;
}

void Engine::drawToBuffer(Time deltaTime, const Camera &camera, std::byte *data, size_t count)
{
    const size_t imageSize = windowExtent.height * windowExtent.width * 4U;
    assert(data != nullptr && count >= imageSize);



    {
        VkFence frameFence = currentFrame().renderFence;
        drawToScreen(deltaTime, camera);
        VK_CHECK(vkWaitForFences(device, 1, &frameFence, true, 1000000000)); //wait until the rendering is done
        vkDeviceWaitIdle(device);
    }
    const VkImage imageToCopy = swapchainInfo.images[swapchainInfo.lastAcquiredImageIndex];

    const vkut::UploadContext uploadContext = getUploadContext();

    vkut::TransitionImageLayoutContext transitionContext
    {
        .uploadContext = uploadContext,
        .image = imageToCopy,
        .fromLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .toLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .format = swapchainInfo.format,
        .mipLevels = 1
    };
    vkut::transitionImageLayout(transitionContext);

    AllocatedBuffer stagingBuffer;
    vkut::submitCommand(uploadContext, [&](VkCommandBuffer cmd) 
    {
        const VkBufferImageCopy imageCopyInfo
        {
            //buffer Offset, RowLength and ImageHeight are 0
            .imageSubresource = 
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = {
                windowExtent.width,
                windowExtent.height,
                1
            }
        };

        stagingBuffer = vkmem::createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, allocator, VMA_MEMORY_USAGE_GPU_TO_CPU);

        vkCmdCopyImageToBuffer(
            cmd,
            imageToCopy,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            stagingBuffer.buffer,
            1,
            &imageCopyInfo
        );
    });

    memcpy(data, vkmem::getMappedData(stagingBuffer), imageSize);

    vkmem::destroyBuffer(allocator, stagingBuffer);
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

void Engine::initImgui()
{
    
    std::array<VkDescriptorPoolSize, 11> poolSizes =
    {
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_SAMPLER,                   .descriptorCount = 1000 }, // overkill but eh
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,    .descriptorCount = 1000 },
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,             .descriptorCount = 1000 },
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,             .descriptorCount = 1000 },
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,      .descriptorCount = 1000 },
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,      .descriptorCount = 1000 },
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            .descriptorCount = 1000 },
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,            .descriptorCount = 1000 },
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,    .descriptorCount = 1000 },
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,    .descriptorCount = 1000 },
        VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,          .descriptorCount = 1000 }
    };

    const VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1000,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiPool));
    QUEUE_DESTROY(vkDestroyDescriptorPool(device, imguiPool, nullptr));
    ImGui::CreateContext();

    ImGui_ImplGlfw_InitForVulkan(window.get(), true);

    ImGui_ImplVulkan_InitInfo initInfo
    {
        .Instance = instance,
        .PhysicalDevice = physicalDevice,
        .Device = device,
        .Queue = graphicsQueue,
        .DescriptorPool = imguiPool,
        .MinImageCount = 3,
        .ImageCount = 3
    };
    ImGui_ImplVulkan_Init(&initInfo, renderPass);

    vkut::submitCommand(getUploadContext(), [](VkCommandBuffer cmd)
        {
            ImGui_ImplVulkan_CreateFontsTexture(cmd); //upload fonts
        });

    ImGui_ImplVulkan_DestroyFontUploadObjects(); //clear up fonts from cpu

    QUEUE_DESTROY(ImGui_ImplVulkan_Shutdown());
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

void Engine::initFramebuffers(bool recreating)
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
    }

    if (!recreating)
    {
        mainDeletionQueue.push([&]()
        {
            for (VkFramebuffer framebuffer : framebuffers) vkut::destroyFramebuffer(device, framebuffer);
            for (VkImageView imageView : swapchainInfo.imageViews) vkut::destroyImageView(device, imageView);
        });
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

MaterialHandle Engine::loadMaterial(const char *vertexModuleName, const char *fragmentModuleName, MeshHandle vertexDescriptionMesh, TextureHandle textureHandle)
{
    const std::string vertexModulePath = getShaderPath(vertexModuleName);
    const std::string fragmentModulePath = getShaderPath(fragmentModuleName);
    const std::optional<VkShaderModule> vertexModule = vkut::createShaderModule(device, vertexModulePath.c_str());
    const std::optional<VkShaderModule> fragmentModule = vkut::createShaderModule(device, fragmentModulePath.c_str());
    if (!vertexModule.has_value())
    {
        Logger::logErrorFormatted("Could not load vertex module at path \"%s\"", vertexModulePath.c_str());
        return MaterialHandle::invalidHandle();
    }

    if (!fragmentModule.has_value())
    {
        Logger::logErrorFormatted("Could not load fragment module at path: \"%s\"", fragmentModulePath.c_str());
        vkut::destroyShaderModule(device, vertexModule.value());
        return MaterialHandle::invalidHandle();
    }

    const VkPipelineLayout pipelineLayout = vkut::createPipelineLayout(device, { globalSetLayout, objectsSetLayout, singleTextureSetLayout }, {});
    QUEUE_DESTROY(vkut::destroyPipelineLayout(device, pipelineLayout));

    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
        vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexModule.value()),
        vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule.value())
    };
    const VertexInputDescription vertexInputDescription = meshes[vertexDescriptionMesh].getDescription();
    const VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputDescription.bindings.size()),
        .pVertexBindingDescriptions = vertexInputDescription.bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputDescription.attributes.size()),
        .pVertexAttributeDescriptions = vertexInputDescription.attributes.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = vkinit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    const VkPipelineRasterizationStateCreateInfo rasterizerStateCreateInfo = vkinit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL);
    
    const VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = vkinit::depthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);
    const VkPipelineMultisampleStateCreateInfo multisamplingStateCreateInfo = vkinit::multisamplingCreateInfo();

    const VkPipelineViewportStateCreateInfo viewportState
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
        //no need to set viewport and scissor - this is done dynamically during command buffer recording
    };

    //setup dummy color blending. We arent using transparent objects yet
    //the blending is just "no blend", but we do write to the color attachment
    const VkPipelineColorBlendAttachmentState colorBlendAttachment = vkinit::colorBlendAttachmentState();
    const VkPipelineColorBlendStateCreateInfo colorBlending
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    const std::array<VkDynamicState, 2> dynamicStates
    {
        VK_DYNAMIC_STATE_VIEWPORT, //so we don't have to recreate the pipelines on swapchain recreation
        VK_DYNAMIC_STATE_SCISSOR,
    };

    const VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    VkGraphicsPipelineCreateInfo pipelineCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = (uint32_t)shaderStages.size(),
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputStateCreateInfo,
        .pInputAssemblyState = &inputAssemblyStateCreateInfo,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizerStateCreateInfo,
        .pMultisampleState = &multisamplingStateCreateInfo,
        .pDepthStencilState = &depthStencilStateCreateInfo,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicStateCreateInfo,
        .layout = pipelineLayout,
        .renderPass = renderPass,
        .subpass = 0,
    };

    //its easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline));
    QUEUE_DESTROY(vkDestroyPipeline(device, pipeline, nullptr));

    vkut::destroyShaderModule(device, vertexModule.value());
    vkut::destroyShaderModule(device, fragmentModule.value());

    Logger::logMessageFormatted("Successfully loaded material with fragment path \"%s\" and vertex path \"%s\"!", fragmentModulePath.c_str(), vertexModulePath.c_str());

    return createMaterial(pipeline, pipelineLayout, textureHandle);
}

void Engine::initDepthResources(bool recreating)
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

    VK_CHECK(vkmem::createImage(allocator, depthImageInfo, depthImageAllocInfo, depthImage, nullptr));

    const VkImageViewCreateInfo depthViewInfo = vkinit::imageviewCreateInfo(depthFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(device, &depthViewInfo, nullptr, &depthImageView));

    if(!recreating)
    {
        QUEUE_DESTROY_REF(vkmem::destroyImage(allocator, depthImage));
        QUEUE_DESTROY_REF(vkDestroyImageView(device, depthImageView, nullptr));
    }

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
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &blockySampler));
    QUEUE_DESTROY(vkDestroySampler(device, blockySampler, nullptr));
}

void Engine::onResize()
{
    vkDeviceWaitIdle(device);

    for (uint32_t i = 0; i < swapchainInfo.images.size(); i++) {
        vkut::destroyFramebuffer(device, framebuffers[i]);
        vkut::destroyImageView(device, swapchainInfo.imageViews[i]);
    }    
    vkmem::destroyImage(allocator, depthImage);
    vkDestroyImageView(device, depthImageView, nullptr);

    VkSwapchainKHR oldSwapchain = swapchainInfo.swapchain;

    auto swapChainResult = createSwapchainInfo(physicalDevice, device, surface, oldSwapchain); //"moves" the old swapchain
    if (swapChainResult.has_value())
    {
        swapchainInfo = swapChainResult.value();
    }
    vkDestroySwapchainKHR(device, oldSwapchain, nullptr); 

    windowExtent = swapchainInfo.extent;

    constexpr bool recreating = true;
    initDepthResources(recreating);
    initFramebuffers(recreating);
}

MeshHandle Engine::loadMesh(const char *name)
{
    const std::string path = getModelPath(name);
    const auto loadResult = Mesh::load(path.c_str());
    if (!loadResult.has_value())
    {
        Logger::logErrorFormatted("Failed to load mesh at path \"%s\"!", path.c_str());
        return MeshHandle::invalidHandle();
    }

    const MeshHandle handle = MeshHandle::getNextHandle();

    meshes[handle] = loadResult.value();
    uploadMesh(meshes[handle]);
    Logger::logMessageFormatted("Successfully loaded mesh at path \"%s\"!", path.c_str());
    return handle;
}

TextureHandle Engine::loadTexture(const char *name)
{
    const std::string path = getTexturePath(name);
    const vkut::ImageLoadContext loadContext
    {
        .device = device,
        .allocator = allocator,
        .uploadFence = uploadFence,
        .uploadCommandPool = uploadCommandPool,
        .queue = graphicsQueue
    };
    const std::optional<AllocatedImage> image = vkut::loadImageFromFile(loadContext, path.c_str());
    if(!image.has_value())
    {
        Logger::logErrorFormatted("Failed to load texture at path \"%s\"!", path.c_str());
        return TextureHandle::invalidHandle();
    }
    
    VkImageView view = vkut::createImageView(device, image.value().image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    QUEUE_DESTROY(vkmem::destroyImage(allocator, image.value()));
    QUEUE_DESTROY(vkut::destroyImageView(device, view));

    TextureHandle handle = TextureHandle::getNextHandle();

    const Texture result = { image.value(), view };
    textures[handle] = result;
    Logger::logMessageFormatted("Successfully loaded texture at path \"%s\"!", path.c_str());
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

vkut::UploadContext Engine::getUploadContext() const
{
    return vkut::UploadContext
    {
        .device = device,
        .uploadFence = uploadFence,
        .commandPool = uploadCommandPool,
        .queue = graphicsQueue
    };
}

void Engine::uploadMesh(Mesh &mesh)
{
    const VmaMemoryUsage vmaStagingBufferUsage = VMA_MEMORY_USAGE_CPU_ONLY; //on CPU RAM
    const VmaMemoryUsage vmaBuffersUsage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    const vkut::UploadContext uploadContext = getUploadContext();

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
