#include "Engine.h"

constexpr const char *monkeyMeshPath = "_assets/models/suzanne.o";
constexpr const char *dragonMeshPath = "_assets/models/dragon.o";
constexpr const char *monkeyVertexShaderPath = "_assets/shaders/monkey.vert.spv";
constexpr const char *dragonVertexShaderPath = "_assets/shaders/dragon.vert.spv";
constexpr const char *fragmentShaderPath = "_assets/shaders/shader.frag.spv";

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    Logger::setVerbosity(Logger::Verbosity::TRIVIAL);

    Engine engine;


    const MeshHandle monkeyMesh = engine.loadMesh(monkeyMeshPath);
    const MeshHandle dragonMesh = engine.loadMesh(dragonMeshPath);

    const MaterialHandle monkeyMaterial = engine.createMaterial(monkeyVertexShaderPath, fragmentShaderPath, monkeyMesh);
    const MaterialHandle dragonMaterial = engine.createMaterial(dragonVertexShaderPath, fragmentShaderPath, dragonMesh);

    
    engine.addRenderObject(dragonMesh, dragonMaterial, mat4x4::identity());
    
    for (int x = -20; x <= 20; x++)
    for (int y = -20; y <= 20; y++) 
    {
        const mat4x4 translation = mat4x4::translate(vec3((float)x, 0.0f, (float)y));
        const mat4x4 scale = mat4x4::scale(vec3(0.2f, 0.2f, 0.2f));
    
        engine.addRenderObject(monkeyMesh, monkeyMaterial, translation * scale);
    }

    engine.run();

    return 0;
}