struct VSOutput
{
	float4 position : SV_POSITION;
	
	[[vk::location(0)]]
	float3 viewDir	: POSITION;
};

struct CubemapData
{
	float4x4 viewProjInverse;
};

[[vk::push_constant]] CubemapData cubemapData;

VSOutput VSMain(uint vertexIndex : SV_VertexID)
{
	VSOutput res = (VSOutput)0;
	res.position = float4(-1.0 + ((vertexIndex & 0x2) * 4.0), -1.0 + (vertexIndex % 2) * 4.0, 0.01, 1.0);
	res.viewDir = mul(cubemapData.viewProjInverse, res.position).xyz;
	return res;
}

[[vk::combinedImageSampler]] TextureCube cubemap			: register(t0);
[[vk::combinedImageSampler]] SamplerState cubemapSampler	: register(s0);

float4 FSMain(VSOutput input) : SV_Target0
{
	return cubemap.Sample(cubemapSampler, input.viewDir);
}