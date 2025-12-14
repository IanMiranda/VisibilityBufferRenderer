#extension GL_EXT_nonuniform_qualifier : require

import Common;
import Lighting;

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

struct ObjectData
{
    float4x4 model;
    uint diffuseMapIndex;
    uint specularMapIndex;
    uint normalMapIndex;
};

[push_constant] ConstantBuffer<ObjectData> gObjectData;
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

float4 Diffuse(float3 N, float3 W, float4 Kd, float4 I, float dist)
{
    float NoW = dot(N, W);

    // Attenuation
    I *= (1.0 / (dist * dist + 0.01));
    float4 diffuse = saturate(NoW) * Kd * I;
    return diffuse;
}

float4 Specular(float3 N, float3 W, float3 V, float4 Ks, float4 I, float alpha)
{
    float3 H = normalize(W + V);
    // float3 R = normalize(reflect(W, N));
    float NoH = dot(N, H);
    // float VoR = dot(V, R);
    float4 specular = I * Ks * pow(saturate(NoH), alpha);
    // float4 specular = I * Ks * pow(max(VoR, 0.0), alpha);
    return specular;
}

/*float GetRecordedDepth(float4 posLight)
{
    float3 shadowCoord = posLight.xyz / posLight.w;
    shadowCoord = float3(shadowCoord.xy * 0.5 + 0.5, shadowCoord.z);
    float bias = 0.015;
    float recordedDepth = gShadowMap.SampleCmp(shadowCoord.xy, shadowCoord.z - bias);
    return recordedDepth;
}*/

[vk_binding(1, 0)] ConstantBuffer<LightData> gLightData;
[vk_binding(2, 0)] SamplerCube gCubemap;
// [vk_binding(3, 0)] Sampler2DShadow gShadowMap;

[shader("fragment")]
float4 FSMain(VSOutput input) : SV_Target0
{
    float alpha = 16.0;

    float4 Ia = float4(0.01, 0.01, 0.01, 1.0);

    float4 Kd = gTextures[gObjectData.diffuseMapIndex].Sample(gTrilinearColor, input.uv);
    float4 Ks = gTextures[gObjectData.specularMapIndex].Sample(gTrilinearColor, input.uv);

    float3 N = gTextures[gObjectData.normalMapIndex].Sample(gTrilinearColor, input.uv).rgb;
    N = N * 2.0 - 1.0;
    N = normalize(mul(input.tbn, N));

    float4 finalColor = 0;
    for (int i = 0; i < gPassData.lightCount; ++i)
    {
        PointLight l = gLightData.lights[i];
        float3 W = normalize(l.posView - input.posView);
        float4 diffuse = Diffuse(N, W, Kd, l.i, distance(gLightData.lights[i].posView, input.posView));

        float3 V = -normalize(input.posView);
        float4 specular = Specular(N, W, V, Ks, l.i, alpha);

        float4 ambient = Kd * Ia;

        // Environment reflections
        float3 Wr = normalize(reflect(-V, N));
        float4 Kr = Ks * 0.1;
        float4 envColor = Kr * gCubemap.Sample(mul(gPassData.viewInverse, float4(Wr, 1.0)).xyz);

        // float recordedDepth = GetRecordedDepth(input.posLight);
        finalColor += ambient + (diffuse + specular + envColor);// * recordedDepth;
    }

    return finalColor;
}