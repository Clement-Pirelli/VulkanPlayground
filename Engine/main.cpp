#include "Engine.h"
#define GLFW_INCLUDE_NONE
#include <glfw/glfw3.h>
#include <Timer.h>
#include <MathUtils.h>
#include <Camera.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>
#include <BMPWriter.h>
#include <ConsoleVariables.h>

#include <renderdoc/RenderDoc.h>
#include <Window.h>
#include <string>

constexpr const char *vertexShaderPath = "shader.vert.spv";
constexpr const char *fragmentShaderPath = "shader.frag.spv";
constexpr const char *texturePath = "minecraft.png";
constexpr const char *meshPath = "minecraft.o";

Camera camera;
ivec2 windowStartingResolution = { 1700 , 900 };
vec2 lastCursorPos = { windowStartingResolution.x()/2.0f, windowStartingResolution.y()/2.0f};
Directions directions{};
bool cursorDisabled = true;
bool showConsoleVariables = false;

RenderDoc doc;

ConsoleVariable<float> cameraSpeed("cameraSpeed", .1f);
ConsoleVariable<bool> renderUI("renderUI", true);

void takeScreenshot(GLFWwindow* window, Engine& engine)
{
    int x, y;
    glfwGetWindowSize(window, &x, &y);
    const size_t screenBytes = x * (unsigned int)y * 4U;
    std::vector<std::byte> screenshot(screenBytes);

    engine.drawToBuffer(Time::now(), camera, screenshot.data(), screenshot.size());

    const std::string fileName = std::string("screenshot") + std::to_string(Time::now().asSeconds()) + ".bmp"; //todo: use date instead
    const bmp::writeInfo writeInfo
    {
        .path = fileName.c_str(),
        .xPixelCount = (uint32_t)x,
        .yPixelCount = (uint32_t)y,
        .contents = reinterpret_cast<bmp::color *>(screenshot.data()),
        .invertedY = true
    };
    bmp::write(writeInfo);

    Logger::logMessageFormatted("Took screenshot at path %s!", fileName.c_str());
}

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
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        return;
    }

    Engine *engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    const bool pressed = action == GLFW_PRESS;
    const bool held = (action == GLFW_PRESS || action == GLFW_REPEAT);
    const bool released = action == GLFW_RELEASE;
    switch (key)
    {
    case GLFW_KEY_C:
    {
        if (pressed)
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
        directions.backwards = held;
    } break;

    case GLFW_KEY_A:
    {
        directions.left = held;
    } break;

    case GLFW_KEY_W:
    { 
        directions.forwards = held;
    } break;

    case GLFW_KEY_D:
    {
        directions.right = held;
    } break;
    case GLFW_KEY_SPACE:
    {
        if(pressed) renderUI.set(!renderUI.get());
    } break;
    case GLFW_KEY_1:
    {
        if (pressed && engine != nullptr) takeScreenshot(window, *engine);
    } break;
    case GLFW_KEY_TAB:
    {
        if (pressed) showConsoleVariables = !showConsoleVariables;
    } break;
    }
}

void UI()
{
    if (showConsoleVariables)
    {
        if(ImGui::Begin("Console variables"))
        {
            ConsoleVariables<int>::forEach([](const std::string &name, int &value)
            {
                ImGui::InputInt(name.c_str(), &value);
            });

            ImGui::Separator();

            ConsoleVariables<float>::forEach([](const std::string &name, float &value)
            {
                ImGui::InputFloat(name.c_str(), &value);
            });

            ImGui::Separator();

            ConsoleVariables<bool>::forEach([](const std::string &name, bool &value)
            {
                ImGui::Checkbox(name.c_str(), &value);
            });
        }
        ImGui::End();
    }
}

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    glfwInit();
    {
        Window window = Window(windowStartingResolution, "Engine");
        window.setCursorCallback(mouseCallback);
        window.setKeyCallback(keyCallback);
        window.setCursorMode(CursorMode::disabled);

        Logger::setVerbosity(Logger::Verbosity::TRIVIAL);

        camera = Camera(vec3(.0f, 1.0f, .0f), vec3(.0f, .0f, -1.0f), vec3(.0f, 1.0f, .0f));

        Engine engine = Engine(window);

        const MeshHandle mesh = engine.loadMesh(meshPath);
        const TextureHandle texture = engine.loadTexture(texturePath);
        const MaterialHandle material = engine.loadMaterial(vertexShaderPath, fragmentShaderPath, mesh, texture);

        engine.addRenderObject(mesh, material, mat4x4::identity(), vec4(1.0f, 1.0f, 1.0f, 1.0f));

        window.setUserData(&engine);

        Time endTime = Time::now();
        do
        {
            const Time deltaTime = Time::now() - endTime;
            glfwPollEvents();

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            if(renderUI.get())
            {
                UI();
                camera.speed = cameraSpeed.get();
            }

            camera.handleMovement(deltaTime, directions);
            engine.drawToScreen(deltaTime, camera);

            endTime = Time::now();

        } while (!window.shouldClose());

    }
    glfwTerminate();
    return 0;
}