#include "Engine.h"
#define GLFW_INCLUDE_NONE
#include <glfw/glfw3.h>
#include <Timer.h>
#include <MathUtils.h>
#include <Camera.h>

constexpr const char *vertexShaderPath = "shader.vert.spv";
constexpr const char *fragmentShaderPath = "shader.frag.spv";
constexpr const char *texturePath = "minecraft.png";
constexpr const char *meshPath = "minecraft.o";

Camera camera;
VkExtent2D windowExtent = { 1700 , 900 };
vec2 lastCursorPos = { windowExtent.width/2.0f, windowExtent.height/2.0f}; 
void mouseCallback(GLFWwindow *window, double xpos, double ypos)
{
    const vec2 offset = 
    {
         (float)xpos - lastCursorPos.x(),
         lastCursorPos.y() - (float)ypos  // reversed since y-coordinates range from bottom to top
    };
    lastCursorPos = { (float)xpos, (float)ypos }; 

    camera.onMouseMovement(offset);
}

Directions directions{};
bool cursorDisabled = true;
//todo: clean this up
void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    const bool direction = (action == GLFW_PRESS || action == GLFW_REPEAT);

    switch (key)
    {
    case GLFW_KEY_ESCAPE:
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
        directions.backwards = direction;
    } break;

    case GLFW_KEY_A:
    {
        directions.left = direction;
    } break;

    case GLFW_KEY_W:
    { 
        directions.forwards = direction;
    } break;

    case GLFW_KEY_D:
    {
        directions.right = direction;
    } break;
    }
}

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    Logger::setVerbosity(Logger::Verbosity::TRIVIAL);
    
    camera = Camera(vec3(.0f, 1.0f, .0f), vec3(.0f, .0f, -1.0f), vec3(.0f, 1.0f, .0f));
    Engine engine = Engine(camera, windowExtent); //TODO: separate engine from window creation!!!

    const MeshHandle mesh = engine.loadMesh(meshPath);
    const TextureHandle texture = engine.loadTexture(texturePath);
    const MaterialHandle material = engine.loadMaterial(vertexShaderPath, fragmentShaderPath, mesh, texture);

    engine.addRenderObject(mesh, material, mat4x4::identity(), vec4(1.0f, 1.0f, 1.0f, 1.0f));

    glfwSetInputMode(engine.getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetKeyCallback(engine.getWindow(), &keyCallback);
    glfwSetCursorPosCallback(engine.getWindow(), mouseCallback);

    Time endTime = Time::now();
    do
    {
        glfwPollEvents();
        const Time deltaTime = Time::now() - endTime;
        camera.handleMovement(deltaTime, directions);
        engine.camera = camera;
        engine.draw(deltaTime);
        endTime = Time::now();
    } while (!engine.shouldQuit());

    return 0;
}