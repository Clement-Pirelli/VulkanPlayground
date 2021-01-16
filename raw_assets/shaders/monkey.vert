#version 450

layout(row_major) uniform;
layout(row_major) buffer;

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vUV;
layout (location = 2) in vec3 vNormal;
layout (location = 3) in vec3 vColor;

layout (location = 0) out vec3 outColor;

layout(set = 0, binding = 0) uniform  CameraBuffer
{   
	mat4 view;
	mat4 proj;
	mat4 viewproj;
} camera;


layout( push_constant ) uniform Constants
{
 vec4 data;
 mat4 render_matrix;
} constants;

void main() 
{	
	mat4 transformMatrix = (camera.viewproj * constants.render_matrix);
	gl_Position = transformMatrix * vec4(vPosition, 1.0f);
	outColor = vNormal;
}