struct VSInput
{
	[[vk::location(0)]]
	float3 position : POSITION;
};

struct ShadowPassData
{
    float4x4 modelViewProjection;
};

[[vk::push_constant]] ShadowPassData gObjectData;

float4 VSMain(VSInput input) : SV_POSITION
{
	return mul(gObjectData.modelViewProjection, float4(input.position, 1.0));
}
