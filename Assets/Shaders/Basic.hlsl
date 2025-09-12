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
	float4x4 normal;
};

[[vk::push_constant]]
MatrixData gMatrices;

VSOutput VSMain(VSInput input)
{
	VSOutput res;
	res.position = mul(gMatrices.mvp, float4(input.position, 1.0));
	res.posView = mul(gMatrices.mv, float4(input.position, 1.0)).xyz;
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
	float3 lightPos;
	float _pad0;
}

float4 FSMain(VSOutput input) : SV_Target0
{
	float alpha = 16;

	float4 I = float4(1.0, 1.0, 1.0, 1.0);
	float4 Ia = float4(0.01, 0.01, 0.01, 1.0);

	float4 Kd = gTexture.Sample(gSampler, input.uv);

	float4 Ks = float4(0.5, 0.5, 0.5, 1.0);

	float3 N = normalize(input.normal);
	float3 W = normalize(lightPos - input.posView);
	float NoW = dot(N, W);
	float4 diffuse = saturate(NoW) * Kd * I;

	float3 V = -normalize(input.posView);
	float3 H = normalize(W + V);
	float NoH = dot(N, H);
	float4 specular = I * Ks * pow(saturate(NoH), alpha);

	float4 ambient = Kd * Ia;

	return ambient + diffuse + specular;
}