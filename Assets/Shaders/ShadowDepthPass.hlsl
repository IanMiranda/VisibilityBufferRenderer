struct VSInput
{
	[[vk::location(0)]]
	float3 position : POSITION;
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

float4 VSMain(VSInput input) : SV_POSITION
{
	return mul(gMatrices.mvp, float4(input.position, 1.0));
}
