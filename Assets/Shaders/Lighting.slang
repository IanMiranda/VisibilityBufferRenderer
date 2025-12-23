module Lighting;

import Math;

public static const int gNumLights = 1024;

public struct PointLight
{
    public float3 posView;
    float _pad0;
    public float4 i;
};

public struct LightData
{
    public PointLight lights[gNumLights];
};

public float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

public float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

public float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
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

public float TrowbridgeReitzGGX(float a, float3 N, float3 H)
{
    float NoH = saturate(dot(N, H));
    float NoH2 = NoH * NoH;

    float num = a * a;
    float denom = NoH2 * (num - 1.0) + 1.0;
    denom *= denom * PI;
    return num / denom;
}

public float SchlickGGX(float NoV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    return NoV / (NoV * (1.0 - k) + k);
}

public float SmithGGX(float roughness, float3 W, float3 V, float3 N)
{
    return SchlickGGX(saturate(dot(N, W)), roughness) *
        SchlickGGX(saturate(dot(N, V)), roughness);
}

public float SchlickGGXIbl(float NoV, float roughness)
{
    float r = roughness;
    float k = (r * r) / 2.0;

    return NoV / (NoV * (1.0 - k) + k);
}

public float SmithGGXIbl(float roughness, float3 W, float3 V, float3 N)
{
    return SchlickGGXIbl(saturate(dot(N, W)), roughness) *
        SchlickGGXIbl(saturate(dot(N, V)), roughness);
}