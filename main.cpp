#include "Engine.h"
#include <glfw/glfw3.h>
#include <Timer.h>
#include <MathUtils.h>
#include <Camera.h>

constexpr const char *monkeyMeshPath = "_assets/models/suzanne.o";
constexpr const char *dragonMeshPath = "_assets/models/dragon.o";
constexpr const char *monkeyVertexShaderPath = "_assets/shaders/monkey.vert.spv";
constexpr const char *dragonVertexShaderPath = "_assets/shaders/dragon.vert.spv";
constexpr const char *fragmentShaderPath = "_assets/shaders/shader.frag.spv";

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
    Engine engine = Engine(camera, windowExtent); //TODO: separate engine from window creation??

    const MeshHandle monkeyMesh = engine.loadMesh(monkeyMeshPath);
    const MaterialHandle monkeyMaterial = engine.createMaterial(monkeyVertexShaderPath, fragmentShaderPath, monkeyMesh);


    const MeshHandle dragonMesh = engine.loadMesh(dragonMeshPath);
    const MaterialHandle dragonMaterial = engine.createMaterial(dragonVertexShaderPath, fragmentShaderPath, dragonMesh);

    
    engine.addRenderObject(dragonMesh, dragonMaterial, mat4x4::identity());
    
    for (int x = -20; x <= 20; x++)
    for (int y = -20; y <= 20; y++) 
    {
        if (x == 0 && y == 0) continue;
        const mat4x4 translation = mat4x4::translate(vec3((float)x, 0.0f, (float)y));
        const mat4x4 scale = mat4x4::scale(vec3(0.2f, 0.2f, 0.2f));
        engine.addRenderObject(monkeyMesh, monkeyMaterial, translation * scale);
    }

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