#version 460

layout(row_major) uniform;
layout(row_major) buffer;

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vUV;
layout (location = 2) in vec3 vNormal;
layout (location = 3) in vec3 vColor;

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec3 outUV;

layout(set = 0, binding = 0) uniform  CameraBuffer
{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
} camera;


layout( push_constant ) uniform Constants //unused at the moment
{
	vec4 data;
	mat4 render_matrix;
} constants;

struct ObjectData
{
	mat4 model;
	vec4 color;
};

//all object matrices
layout(std140,set = 1, binding = 0) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} objectBuffer;



void main() 
{
	ObjectData objectData = objectBuffer.objects[gl_BaseInstance];
	mat4 modelMatrix = objectData.model;
	mat4 transformMatrix = (camera.viewproj * modelMatrix);
	gl_Position = transformMatrix * vec4(vPosition, 1.0);
	outColor = objectData.color.xyz * dot(vNormal, vec3(1.0,.0,.0));
	outUV = vUV;
}