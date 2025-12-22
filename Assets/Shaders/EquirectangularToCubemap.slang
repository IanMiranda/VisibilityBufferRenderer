struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 localPos : POSITION;
};

struct EqMapData
{
	float4x4 viewProj;
};

[push_constant] ConstantBuffer<EqMapData> gEqMapData;

[shader("vertex")]
VSOutput VSMain(float3 pos : POSITION)
{
    pos.y *= -1.0f;
	VSOutput res = (VSOutput)0;
    res.pos = mul(gEqMapData.viewProj, float4(pos, 1.0)).xyww;
    res.localPos = pos;
	return res;
}

[vk_binding(0, 0)] Sampler2D gEqMap;

static const float2 inverseAtan = float2(0.1591, 0.3183);
float2 SampleSphericalMap(float3 dir)
{
    float2 uv = float2(atan2(dir.x, dir.z), asin(dir.y));
    uv *= inverseAtan;
    uv += 0.5;
    return uv;
}

[shader("fragment")]
float4 FSMain(VSOutput input) : SV_Target0
{
    float2 uv = SampleSphericalMap(normalize(input.localPos));
    float3 color = gEqMap.Sample(uv).rgb;
    return float4(color, 1.0);
}