struct VSInput
{
	[[vk::location(0)]]
	float3 position : POSITION;
	[[vk::location(1)]]
	float4 color	: COLOR;
	[[vk::location(2)]]
	float2 uv		: TEXCOORD;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	
	[[vk::location(0)]]
	float4 color	: COLOR;

	[[vk::location(1)]]
	float2 uv		: TEXCOORD;
};

struct MatrixData
{
	float4x4 model;
	float4x4 view;
	float4x4 projection;
};

[[vk::push_constant]]
MatrixData gMatrices;

VSOutput VSMain(VSInput input)
{
	VSOutput res;
	res.position = mul(gMatrices.model, float4(input.position, 1.0));
	res.position = mul(gMatrices.view, res.position);
	res.position = mul(gMatrices.projection, res.position);
	res.color = input.color;
	res.uv = input.uv;
	return res;
}

[[vk::combinedImageSampler]]
Texture2D gTexture : register(t0);
[[vk::combinedImageSampler]]
SamplerState gSampler : register(s0);

float4 FSMain(VSOutput input) : SV_Target0
{
	return gTexture.Sample(gSampler, input.uv);
}