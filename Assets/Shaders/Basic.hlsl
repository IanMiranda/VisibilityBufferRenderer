struct VSInput
{
	[[vk::location(0)]]
	float3 position : POSITION;
	[[vk::location(1)]]
	float4 color	: COLOR;
	[[vk::location(2)]]
	float2 uv		: TEXCOORD;
	[[vk::location(3)]]
	float3 normal	: NORMAL;
};

struct VSOutput
{
	float4 position	: SV_POSITION;

	[[vk::location(0)]]
	float3 posView	: POSITION0;

	[[vk::location(1)]]
	float4 color	: COLOR;

	[[vk::location(2)]]
	float2 uv		: TEXCOORD;

	[[vk::location(3)]]
	float3 normal	: NORMAL;

	[[vk::location(4)]]
	float4 posLight : POSITION1;

};

struct MatrixData
{
	float4x4 mv;
	float4x4 mvp;
	float4x4 normal;
	float4x4 mvpLight;
};

[[vk::push_constant]]
MatrixData gMatrices;

VSOutput VSMain(VSInput input)
{
	VSOutput res;
	res.position = mul(gMatrices.mvp, float4(input.position, 1.0));
	res.posView = mul(gMatrices.mv, float4(input.position, 1.0)).xyz;
	res.posLight = mul(gMatrices.mvpLight, float4(input.position, 1.0));
	res.posLight.y *= -1.0f;
	res.color = input.color;
	res.uv = input.uv;
	// res.normal = normalize(mul(gMatrices.mv, float4(input.normal, 0.0)).xyz);
	res.normal = normalize(mul((float3x3)gMatrices.normal, input.normal)); // TODO: Convert in glm code
	return res;
}

[[vk::combinedImageSampler]]
Texture2D gTexture : register(t0, space1);
[[vk::combinedImageSampler]]
SamplerState gSampler : register(s0, space1);

cbuffer LightingData : register(b0, space0)
{
	float4x4 vInverse;
	float3 lightDir;
	float _pad0;
}

[[vk::combinedImageSampler]]
TextureCube gCubemap : register(t1, space0);

[[vk::combinedImageSampler]]
SamplerState gCubemapSampler: register(s1, space0);

[[vk::combinedImageSampler]]
Texture2D gDepthMap : register(t2, space0);

[[vk::combinedImageSampler]]
SamplerComparisonState gDepthSampler : register(s2, space0);

float4 FSMain(VSOutput input) : SV_Target0
{
	float alpha = 16;

	float4 I = float4(1.0, 1.0, 1.0, 1.0);
	float4 Ia = float4(0.01, 0.01, 0.01, 1.0);

	float4 Kd = gTexture.Sample(gSampler, input.uv) * 0.1;

	float4 Ks = float4(0.9, 0.9, 0.9, 1.0);

	float3 N = normalize(input.normal);
	float3 W = normalize(lightDir); // normalize(lightPos - input.posView);
	float NoW = dot(N, W);

	// Attenuation
	// float dist = distance(lightPos, input.posView);
	// I *= (1.0 / (dist * dist + 0.01));
	float4 diffuse = saturate(NoW) * Kd * I;

	float3 V = -normalize(input.posView);
	float3 H = normalize(W + V);
	float3 R = normalize(reflect(W, N));
	float NoH = dot(N, H);
	float VoR = dot(V, R);
	float4 specular = I * Ks * pow(saturate(NoH), alpha);
	// float4 specular = I * Ks * pow(saturate(VoR), alpha);

	float4 ambient = Kd * Ia;

	// Environment reflections
	float3 Wr = normalize(reflect(-V, N));
	float4 Kr = Ks;
	float4 cosTheta = saturate(dot(Wr, W));
	float4 envReflection = float4((Kr * (cosTheta > 0.99)).xyz, 1.0);
	float4 envColor = Kr * gCubemap.Sample(gCubemapSampler, mul(vInverse, float4(Wr, 1.0)).xyz);

	float3 shadowCoord = input.posLight.xyz / input.posLight.w;
	shadowCoord = float3(shadowCoord.xy * 0.5 + 0.5, shadowCoord.z);
	float bias = 0.015;
	float recordedDepth = gDepthMap.SampleCmp(gDepthSampler, shadowCoord.xy, shadowCoord.z - bias);
	return ambient + (diffuse + specular + envColor) * recordedDepth;
}