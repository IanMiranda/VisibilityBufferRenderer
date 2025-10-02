#version 450

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inUv;
layout (location = 3) in vec3 inNormal;
layout (location = 4) in vec3 inTangent;
layout (location = 5) in vec3 inBitangent;

layout (location = 0) out vec3 outPosView;
layout (location = 1) out vec4 outColor;
layout (location = 2) out vec2 outUv;
layout (location = 3) out vec3 outNormal;
layout (location = 4) out vec4 outPosLight;
layout (location = 5) out mat3 outTbn;

layout (push_constant) uniform ObjectData
{
	mat4 model;
	uint textureIndex;
	uint normalMapIndex;
} gObjectData;

layout (binding = 0, set = 0) uniform GlobalPassData
{
	mat4 view;
	mat4 viewProj;
	mat4 viewProjLight;
	mat4 viewInverse;
	vec3 lightDir;
	float _pad0;
} gPassData;

void VSMain()
{
	mat4 mv = gPassData.view * gObjectData.model;
	mat4 mvp = gPassData.viewProj * gObjectData.model;
	mat4 mvpLight = gPassData.viewProjLight * gObjectData.model;

	vec3 T = normalize(mv * vec4(inTangent, 0.0)).xyz;
	vec3 B = normalize(mv * vec4(inBitangent, 0.0)).xyz;
	vec3 N = normalize(mv * vec4(inNormal, 0.0)).xyz;
	outTbn = mat3(T, B, N);

	gl_Position = mvp * vec4(inPosition, 1.0);
	outPosView = vec3(mv * vec4(inPosition, 1.0));
	outPosLight = mvpLight * vec4(inPosition, 1.0);
	outPosLight.y *= -1.0f;
	outColor = inColor;
	outUv = inUv;
}
