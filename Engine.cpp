#include "Engine.h"
#include "VkBootstrap.h"
#include "glfw/glfw3.h"
#include <optional>
#include "Logger/Logger.h"

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
    do 
    {
        glfwPollEvents();
        draw();
    } while (!glfwWindowShouldClose(window));
}

Engine::Engine()
{
    initVulkan();

    auto swapChainResult = createSwapchainInfo(physicalDevice, device, surface);
    if (swapChainResult.has_value())
    {
        swapchainInfo = swapChainResult.value();
    }

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
        vkDestroyFence(device, renderFence, nullptr);
        vkDestroySemaphore(device, presentSemaphore, nullptr);
        vkDestroySemaphore(device, renderSemaphore, nullptr);

        vkDestroyCommandPool(device, commandPool, nullptr);

        //images are owned by the swapchain and are destroyed when the swapchain is
        vkDestroySwapchainKHR(device, swapchainInfo.swapchain, nullptr);
        vkut::destroyRenderPass(device, renderPass);
        for (size_t i = 0; i < swapchainInfo.imageViews.size(); i++)
        {
            vkut::destroyFramebuffer(device, framebuffers[i]);
            vkDestroyImageView(device, swapchainInfo.imageViews[i], nullptr);
        }

        destroySurface(instance, surface);
        vkDestroyDevice(device, nullptr);
#ifndef NDEBUG
        vkb::destroy_debug_utils_messenger(instance, debugMessenger);
#endif
        vkDestroyInstance(instance, nullptr);
        destroyWindow(window);
        Logger::logMessage("Successfully destroyed vulkan resources!");
    }
}

void Engine::draw()
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

        const VkClearValue clearValue
        {
            .color = { { 0.0f, 0.0f, flash, 1.0f } }
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
            .clearValueCount = 1,
            .pClearValues = &clearValue,

        };

        vkCmdBeginRenderPass(mainCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            
            //nothing here yet
            
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

    // this will put the image we just rendered to into the visible window
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

    //increase the number of frames drawn
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
#ifndef NDEBUG
    debugMessenger = vkbInstance.debug_messenger;
#endif

    window = createWindow("Hello Vulkan", windowExtent.width, windowExtent.height);
    surface = createSurface(instance, window);

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

    auto graphicsQueueResult = vkbDevice.get_queue(vkb::QueueType::graphics);
    VKB_CHECK(graphicsQueueResult, "Failed to get graphics queue");
    graphicsQueue = graphicsQueueResult.value();

    auto graphicsQueueFamilyResult = vkbDevice.get_queue_index(vkb::QueueType::graphics);
    VKB_CHECK(graphicsQueueFamilyResult, "Failed to get graphics queue index");
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
    // the renderpass will use this color attachment.
    VkAttachmentDescription colorAttachment
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

    renderPass = vkut::createRenderPass(device, {colorAttachment});

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
            .colorViews = { swapchainInfo.imageViews[i] }
        };
        framebuffers[i] = vkut::createRenderPassFramebuffer(framebufferInfo);
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

    const VkSemaphoreCreateInfo semaphoreCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        //no flags
    };
    VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &presentSemaphore));
    VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderSemaphore));
}
