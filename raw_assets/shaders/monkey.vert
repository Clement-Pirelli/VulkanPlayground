#version 460

layout(row_major) uniform;
layout(row_major) buffer;

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec2 vUV;
layout (location = 2) in vec3 vNormal;
layout (location = 3) in vec3 vColor;

layout (location = 0) out vec3 outColor;

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
};

//all object matrices
layout(std140,set = 1, binding = 0) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} objectBuffer;



void main() 
{
	int matrixIndex = gl_BaseInstance; //bit of a hack, gl_BaseInstance is a "free" int we can send to the shader in non-instanced rendering
	mat4 modelMatrix = objectBuffer.objects[matrixIndex].model;
	mat4 transformMatrix = (camera.viewproj * modelMatrix);
	gl_Position = transformMatrix * vec4(vPosition, 1.0f);
	outColor = vNormal;
}