#version 450

layout(row_major) uniform;
layout(row_major) buffer;

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;

layout (location = 0) out vec3 outColor;

layout ( push_constant) uniform Constants
{
	vec4 data;
	mat4 matrix;
} constants;

void main() 
{	
	gl_Position = constants.matrix * vec4(vPosition, 1.0f);
	outColor = vNormal;
}