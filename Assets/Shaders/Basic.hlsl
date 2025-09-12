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
	float3 posView	: POSITION;

	[[vk::location(1)]]
	float4 color	: COLOR;

	[[vk::location(2)]]
	float2 uv		: TEXCOORD;

	[[vk::location(3)]]
	float3 normal	: NORMAL;

};

struct MatrixData
{
	float4x4 mv;
	float4x4 mvp;
	float3x3 normal;
};

[[vk::push_constant]]
MatrixData gMatrices;

VSOutput VSMain(VSInput input)
{
	VSOutput res;
	res.position = mul(gMatrices.mvp, float4(input.position, 1.0));
	res.posView = float3(mul(gMatrices.mv, float4(input.position, 1.0)).xyz);
	res.color = input.color;
	res.uv = input.uv;
	res.normal = mul(gMatrices.normal, input.normal);
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