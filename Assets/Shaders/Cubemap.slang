struct VSOutput
{
	float4 position : SV_POSITION;
	
	float3 viewDir	: POSITION;
};

struct CubemapData
{
	float4x4 viewProjInverse;
};

[push_constant] ConstantBuffer<CubemapData> gCubemapData;

[shader("vertex")]
VSOutput VSMain(uint vertexIndex : SV_VertexID)
{
	VSOutput res = (VSOutput)0;
	res.position = float4(-1.0 + ((vertexIndex & 0x2) * 4.0), -1.0 + (vertexIndex % 2) * 4.0, 0.01, 1.0);
	res.viewDir = mul(gCubemapData.viewProjInverse, res.position).xyz;
	return res;
}

[vk_binding(0, 0)] SamplerCube gCubemap;

[shader("fragment")]
float4 FSMain(VSOutput input) : SV_Target0
{
	return gCubemap.Sample(input.viewDir);
}