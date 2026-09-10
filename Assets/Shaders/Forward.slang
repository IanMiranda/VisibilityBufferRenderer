import Lighting;
import Math;

struct Vertex
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

struct FwMatrixData
{
    float4x4 view;
    float4x4 viewProj;
    float4x4 viewProjLight;
    float4x4 viewInverse;
};

static const int gNumLights = 2000;

struct FwLightData
{
    PointLight lights[gNumLights];
    uint lightCount;
};

struct FwPassData
{
    ConstantBuffer<FwMatrixData> matrices;
    ConstantBuffer<FwLightData> lightData;
    SamplerCube irradianceMap;
    SamplerCube prefilterMap;
    Sampler2D brdfLut;
}


struct FwBindlessData
{
    SamplerState trilinearColor;
    Texture2D textures[];
}

struct FwObjectData
{
    float4x4 model;
    Vertex *vertexBuffer;
    uint albedoMapIndex;
    uint metallicRoughnessMapIndex;
    uint normalMapIndex;
    uint aoMapIndex;
    uint emissiveMapIndex;
};

ParameterBlock<FwPassData> gPass;
ParameterBlock<FwBindlessData> gBindless;

[shader("vertex")]
VSOutput VSMain(
    uniform FwObjectData object,
    uint vertex : SV_VulkanVertexID)
{
    VSOutput output = (VSOutput)0;
    Vertex input = object.vertexBuffer[vertex];

    float4x4 model = object.model;
    float4x4 mv = mul(gPass.matrices.view, model);
    float4x4 mvp = mul(gPass.matrices.viewProj, model);
    float4x4 mvpLight = mul(gPass.matrices.viewProjLight, model);

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

float3 CalculateRadiance(float distance, PointLight light)
{
    float attenuation = 1.0 / (distance * distance);
    return light.i.rgb * attenuation;
}

float3 FresnelSchlick(float3 F0, float3 W, float3 H)
{
    return F0 + (1 - F0) * pow(1 - saturate(dot(W, H)), 5.0);
}

float3 FresnelSchlickRoughness(float3 F0, float3 W, float3 H, float roughness)
{
    return F0 + (max(float3(1 - roughness), F0) - F0) * pow(1 - saturate(dot(W, H)), 5.0);
}

float4 SampleMaterialTexture(uint index, float2 uv)
{
    return gBindless.textures[NonUniformResourceIndex(index)].Sample(gBindless.trilinearColor, uv);
}

[shader("fragment")]
float4 FSMain(
    uniform FwObjectData object,
    VSOutput input) : SV_Target0
{
    static const float gamma = 2.2;

    float3 albedo = FromSrgb(SampleMaterialTexture(object.albedoMapIndex, input.uv).rgb, gamma);
    float metallic = SampleMaterialTexture(object.metallicRoughnessMapIndex, input.uv).r;
    float roughness = SampleMaterialTexture(object.metallicRoughnessMapIndex, input.uv).g;
    float ao = SampleMaterialTexture(object.aoMapIndex, input.uv).r;

    float3 N = SampleMaterialTexture(object.normalMapIndex, input.uv).rgb;
    N = N * 2.0 - 1.0;
    N = normalize(mul(input.tbn, N));

    float3 V = normalize(-input.posView);

    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

    float3 finalColor = 0;
    for (int i = 0; i < gPass.lightData.lightCount; ++i)
    {
        PointLight l = gPass.lightData.lights[i];

        float3 W = normalize(l.posView - input.posView);
        float3 H = normalize(W + V);

        float lightDistance = distance(gPass.lightData.lights[i].posView, input.posView);

        var radiance = CalculateRadiance(lightDistance, gPass.lightData.lights[i]);

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

    float3 Ks = FresnelSchlickRoughness(F0, N, V, roughness);
    float3 Kd = float3(1.0) - Ks;
    float3 diffuseIrradiance = gPass.irradianceMap.Sample(N).rgb;
    float3 diffuseAmbient = diffuseIrradiance * albedo;

    static const float maxReflectionLevel = 4.0;
    float3 R = normalize(reflect(-V, N));
    R = mul(gPass.matrices.viewInverse, float4(R, 0.0)).xyz;
    const float3 prefilterColor = gPass.prefilterMap.SampleLevel(R, roughness * maxReflectionLevel).rgb;
    const float3 F = FresnelSchlickRoughness(F0, V, N, roughness);
    float2 brdfValue = gPass.brdfLut.Sample(float2(saturate(dot(N, V)), roughness)).rg;
    float3 specular = prefilterColor * (F * brdfValue.x + brdfValue.y);

    float3 ambient = (diffuseAmbient * Kd + specular) * ao;
    float3 emissive = FromSrgb(SampleMaterialTexture(object.emissiveMapIndex, input.uv).rgb, gamma);

    float3 color = ambient + finalColor + emissive;
    color = color / (color + float3(1.0));
    color = ToSrgb(color, gamma);
    return float4(color, 1.0);
}