import Math;
import Lighting;

struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 localPos : POSITION;
};

struct PrefilterData
{
    float4x4 viewProj;
    float roughness;
};

[push_constant] ConstantBuffer<PrefilterData> gPrefilterData;

[shader("vertex")]
VSOutput VSMain(float3 pos : POSITION)
{
    VSOutput res = (VSOutput)0;
    res.pos = mul(gPrefilterData.viewProj, float4(pos, 1.0)).xyww;
    res.localPos = pos;
	return res;
}

[vk_binding(0, 0)] SamplerCube gEnvMap;

[shader("fragment")]
float4 FSMain(VSOutput input) : SV_Target0
{
    const float3 N = normalize(input.localPos);
    const float3 R = N;
    const float3 V = R;

    const uint sampleCount = 4096;
    float totalWeight = 0.0;
    float3 prefilterColor = float3(0.0);
    for (uint i = 0; i < sampleCount; ++i)
    {
        float2 Xi = Hammersley(i, sampleCount);
        float3 H = ImportanceSampleGGX(Xi, N, gPrefilterData.roughness * gPrefilterData.roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = saturate(dot(N, L));
        if (NoL > 0.0)
        {
            prefilterColor += gEnvMap.Sample(L).rgb * NoL;
            totalWeight += NoL;
        }
    }
    prefilterColor = prefilterColor / totalWeight;
    return float4(prefilterColor, 1.0);
}