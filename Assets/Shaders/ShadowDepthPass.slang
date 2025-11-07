struct VSInput
{
	float3 position : POSITION;
};

struct ShadowPassData
{
    float4x4 modelViewProjection;
};

[push_constant] ConstantBuffer<ShadowPassData> gObjectData;

[shader("vertex")]
float4 VSMain(VSInput input) : SV_POSITION
{
	return mul(gObjectData.modelViewProjection, float4(input.position, 1.0));
}
