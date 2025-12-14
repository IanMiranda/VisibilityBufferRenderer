#extension GL_EXT_nonuniform_qualifier : require

import Common;
import Lighting;

static const float PI = 3.1415926538;

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct VSOutput
{
    float4 svPosition : SV_Position;
    float3 posView : POSITION0;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 posLight : POSITION1;
    float3x3 tbn : TBN;
};

public struct GlobalPassData
{
    public float4x4 view;
    public float4x4 viewProj;
    public float4x4 viewProjLight;
    public float4x4 viewInverse;
    public uint lightCount;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct PbrObjectData
{
    float4x4 model;
    uint albedoMapIndex;
    uint metallicMapIndex;
    uint roughnessMapIndex;
    uint normalMapIndex;
    uint aoMapIndex;
};

[push_constant] ConstantBuffer<PbrObjectData> gObjectData;
[vk_binding(0, 0)] ConstantBuffer<GlobalPassData> gPassData;

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output = (VSOutput)0;

    float4x4 mv = mul(gPassData.view, gObjectData.model);
    float4x4 mvp = mul(gPassData.viewProj, gObjectData.model);
    float4x4 mvpLight = mul(gPassData.viewProjLight, gObjectData.model);

    float3 T = normalize(mul(mv, float4(input.tangent, 0.0))).xyz;
    float3 B = normalize(mul(mv, float4(input.bitangent, 0.0))).xyz;
    float3 N = normalize(mul(mv, float4(input.normal, 0.0))).xyz;
    output.tbn = transpose(float3x3(T, B, N));

    output.svPosition = mul(mvp, float4(input.position, 1.0));
    output.posView = mul(mv, float4(input.position, 1.0)).xyz;
    output.posLight = mul(mvpLight, float4(input.position, 1.0));
    output.posLight.y *= -1.0f;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

[vk_binding(1, 0)] ConstantBuffer<LightData> gLightData;

float3 CalculateRadiance(float distance, PointLight light)
{
    float attenuation = 1.0 / (distance * distance);
    return light.i.rgb * attenuation;
}

float3 FresnelSchlick(float3 F0, float3 W, float3 H)
{
    return F0 + (1 - F0) * pow(1 - saturate(dot(W, H)), 5.0);
}

float TrowbridgeReitzGGX(float a, float3 N, float3 H)
{
    float NoH = saturate(dot(N, H));
    float NoH2 = NoH * NoH;

    float num = a * a;
    float denom = NoH2 * (num - 1.0) + 1.0;
    denom *= denom * PI;
    return num / denom;
}

float SchlickGGX(float NoV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    return NoV / (NoV * (1.0 - k) + k);
}

float SmithGGX(float roughness, float3 W, float3 V, float3 N)
{
    return SchlickGGX(saturate(dot(N, W)), roughness) *
        SchlickGGX(saturate(dot(N, V)), roughness);
}

[shader("fragment")]
float4 FSMain(VSOutput input) : SV_Target0
{
    float3 albedo = gTextures[NonUniformResourceIndex(gObjectData.albedoMapIndex)].Sample(input.uv).rgb;
    float metallic = gTextures[NonUniformResourceIndex(gObjectData.metallicMapIndex)].Sample(input.uv).r;
    float roughness = gTextures[NonUniformResourceIndex(gObjectData.roughnessMapIndex)].Sample(input.uv).r;
    float ao = gTextures[NonUniformResourceIndex(gObjectData.aoMapIndex)].Sample(input.uv).r;

    float3 N = gTextures[NonUniformResourceIndex(gObjectData.normalMapIndex)].Sample(input.uv).rgb;
    N = N * 2.0 - 1.0;
    N = normalize(mul(input.tbn, N));

    float3 V = -normalize(input.posView);

    float3 finalColor = 0;
    for (int i = 0; i < gPassData.lightCount; ++i)
    {
        PointLight l = gLightData.lights[i];

        float3 W = normalize(l.posView - input.posView);
        float3 H = normalize(W + V);

        float lightDistance = distance(gLightData.lights[i].posView, input.posView);

        var radiance = CalculateRadiance(lightDistance, gLightData.lights[i]);

        float3 F0 = float3(0.04);
        F0 = lerp(F0, albedo, metallic);
        float3 F = FresnelSchlick(F0, W, H);

        float D = TrowbridgeReitzGGX(roughness * roughness, N, H);
        float G = SmithGGX(roughness, W, V, N);
        float3 num = D * G * F;
        float denom = 4.0 * saturate(dot(N, V)) * saturate(dot(N, W)) + 0.0001;

        float3 specularTerm = num / denom;
        float3 Ks = F;
        float3 Kd = float3(1.0) - Ks;
        Kd *= 1.0 - metallic;

        float NoW = saturate(dot(N, W));
        finalColor += (Kd * albedo / PI + specularTerm) * radiance * NoW;
    }

    float3 ambient = float3(0.03) * albedo * ao;
    float3 color = ambient + finalColor;
    color = color / (color + float3(1.0));
    color = pow(color, float3(1.0 / 2.2));
    return float4(color, 1.0);
}