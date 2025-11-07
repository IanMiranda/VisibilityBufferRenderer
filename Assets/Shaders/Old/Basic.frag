#version 450

#extension GL_EXT_nonuniform_qualifier : require

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

layout (binding = 1, set = 0) uniform samplerCube gCubemap;
layout (binding = 2, set = 0) uniform sampler2DShadow gDepthMap;

layout (binding = 0, set = 1) uniform sampler2D gTextures[];

layout (location = 0) in vec3 inPosView;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inUv;
layout (location = 3) in vec3 inNormal;
layout (location = 4) in vec4 inPosLight;
layout (location = 5) in mat3 inTbn;

layout (location = 0) out vec4 outColor;

void FSMain()
{
	float alpha = 16.0;

	vec4 I = vec4(1.0, 1.0, 1.0, 1.0);
	vec4 Ia = vec4(0.01, 0.01, 0.01, 1.0);

	vec4 Kd = texture(gTextures[gObjectData.textureIndex], inUv) * 0.1;

	vec4 Ks = vec4(0.9, 0.9, 0.9, 1.0);

	vec3 N = texture(gTextures[gObjectData.normalMapIndex], inUv).rgb;
	N = N * 2.0 - 1.0;
    N = normalize(inTbn * N);
	
	vec3 W = normalize(gPassData.lightDir);
	float NoW = dot(N, W);

	// Attenuation
	// float dist = distance(lightPos, input.posView);
	// I *= (1.0 / (dist * dist + 0.01));
	vec4 diffuse = max(NoW, 0.0) * Kd * I;

	vec3 V = -normalize(inPosView);
	vec3 H = normalize(W + V);
	vec3 R = normalize(reflect(W, N));
	float NoH = dot(N, H);
	float VoR = dot(V, R);
	vec4 specular = I * Ks * pow(max(NoH, 0.0), alpha);
	// float4 specular = I * Ks * pow(max(VoR, 0.0), alpha);

	vec4 ambient = Kd * Ia;

	// Environment reflections
	vec3 Wr = normalize(reflect(-V, N));
	vec4 Kr = Ks * 0.1;
	vec4 envColor = Kr * texture(gCubemap, (gPassData.viewInverse * vec4(Wr, 1.0)).xyz);

	vec3 shadowCoord = inPosLight.xyz / inPosLight.w;
	shadowCoord = vec3(shadowCoord.xy * 0.5 + 0.5, shadowCoord.z);
	float bias = 0.015;
	float recordedDepth = texture(gDepthMap, vec3(shadowCoord.xy, shadowCoord.z - bias));
	outColor = ambient + (diffuse + specular + envColor) * recordedDepth;
}