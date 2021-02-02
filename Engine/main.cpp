#include "Engine.h"
#define GLFW_INCLUDE_NONE
#include <glfw/glfw3.h>
#include <Timer.h>
#include <MathUtils.h>
#include <Camera.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>
#include <Window.h>
#include <BMPWriter.h>
#include <string>
#include <renderdoc/RenderDoc.h>

constexpr const char *vertexShaderPath = "shader.vert.spv";
constexpr const char *fragmentShaderPath = "shader.frag.spv";
constexpr const char *texturePath = "minecraft.png";
constexpr const char *meshPath = "minecraft.o";

Camera camera;
ivec2 windowStartingResolution = { 1700 , 900 };
vec2 lastCursorPos = { windowStartingResolution.x()/2.0f, windowStartingResolution.y()/2.0f};
Directions directions{};
bool cursorDisabled = true;

RenderDoc doc;

void mouseCallback(GLFWwindow *window, double xpos, double ypos)
{
    const vec2 offset = 
    {
         (float)xpos - lastCursorPos.x(),
         lastCursorPos.y() - (float)ypos  // reversed since y-coordinates range from bottom to top
    };
    lastCursorPos = { (float)xpos, (float)ypos }; 

    if(cursorDisabled) camera.onMouseMovement(offset);
}

//todo: clean this up
void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    Engine *engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    const bool pressing = (action == GLFW_PRESS || action == GLFW_REPEAT);
    switch (key)
    {
    case GLFW_KEY_C:
    {
        if (action == GLFW_PRESS)
        {
            cursorDisabled = !cursorDisabled;
            if (cursorDisabled)
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            else
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    } break;
    case GLFW_KEY_S:
    {
        directions.backwards = pressing;
    } break;

    case GLFW_KEY_A:
    {
        directions.left = pressing;
    } break;

    case GLFW_KEY_W:
    { 
        directions.forwards = pressing;
    } break;

    case GLFW_KEY_D:
    {
        directions.right = pressing;
    } break;
    case GLFW_KEY_SPACE:
    {
        if(action == GLFW_PRESS) engine->settings.renderUI = !engine->settings.renderUI;
    } break;
    case GLFW_KEY_1:
    {
        if (action == GLFW_PRESS)
        {
            int x, y;
            glfwGetWindowSize(window, &x, &y);
            std::vector<std::byte> screenshot(x * y * 4U);

            //doc.startCapture();
            engine->drawToBuffer(Time::now(), camera, screenshot.data(), screenshot.size());
            //doc.stopCapture();

            std::string fileName = std::string("screenshot") + std::to_string(Time::now().asSeconds()) + ".bmp";
            writeBMP(fileName.c_str(), x, y, reinterpret_cast<color *>(screenshot.data()), true);
            Logger::logMessageFormatted("Took screenshot at path %s!", fileName.c_str());
        }
    } break;
    }
}

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    glfwInit();
    {
        Window window = Window(windowStartingResolution, "Engine");
        Logger::setVerbosity(Logger::Verbosity::TRIVIAL);

        camera = Camera(vec3(.0f, 1.0f, .0f), vec3(.0f, .0f, -1.0f), vec3(.0f, 1.0f, .0f));
        Engine engine = Engine(window);

        const MeshHandle mesh = engine.loadMesh(meshPath);
        const TextureHandle texture = engine.loadTexture(texturePath);
        const MaterialHandle material = engine.loadMaterial(vertexShaderPath, fragmentShaderPath, mesh, texture);

        engine.addRenderObject(mesh, material, mat4x4::identity(), vec4(1.0f, 1.0f, 1.0f, 1.0f));

        window.setCursorCallback(mouseCallback);
        window.setKeyCallback(keyCallback);
        window.setCursorMode(CursorMode::disabled);
        window.setUserData(&engine);

        Time endTime = Time::now();
        do
        {
            if (engine.settings.renderUI)
            {
                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                ImGui::ShowDemoWindow();
            }

            glfwPollEvents();
            const Time deltaTime = Time::now() - endTime;
            camera.handleMovement(deltaTime, directions);
            engine.drawToScreen(deltaTime, camera);
            endTime = Time::now();

        } while (!window.shouldClose());

    }
    glfwTerminate();
    return 0;
}