import Math;

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

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    const float a = roughness * roughness;

    const float phi = 2.0 * PI * Xi.x;
    const float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta); // Simple trig identity

    const float3 H = float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );

    const float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, N));
    const float3 bitangent = normalize(cross(N, tangent));

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

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