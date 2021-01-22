#version 450

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

layout(set = 0, binding = 1) uniform  SceneData
{
	 vec4 example1;
	 vec4 example2;
	 vec4 ambientColor;
	 vec4 sunlightDirection; //w for sun power
	 vec4 sunlightColor;
} sceneData;

layout(set = 2, binding = 0) uniform sampler2D tex;

void main() 
{
	vec3 color = texture(tex, inUV).xyz;
	outFragColor = vec4(color, 1.0);
}