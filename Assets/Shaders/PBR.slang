#extension GL_EXT_nonuniform_qualifier : require

import Common;
import Lighting;
import Math;
import Mesh;

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
    uint instance : INSTANCE;
};

struct PbrObjectData
{
    float4x4 model;
    Vertex *vertexData;
    Meshlet *meshletData;
    uint albedoMapIndex;
    uint metallicMapIndex;
    uint roughnessMapIndex;
    uint normalMapIndex;
    uint aoMapIndex;
    uint emissiveMapIndex;
};

public struct GlobalPassData
{
    public float4x4 view;
    public float4x4 viewProj;
    public float4x4 viewProjLight;
    public float4x4 viewInverse;
    PbrObjectData *objectData;
    public uint lightCount;
    uint _pad0;
};

[vk_binding(0, 0)] ConstantBuffer<GlobalPassData> gPassData;

/*[shader("vertex")]
VSOutput VSMain(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
    VSOutput output = (VSOutput)0;
    Vertex input = gPassData.objectData[instance].vertexData[vertex];

    float4x4 model = gPassData.objectData[instance].model;
    float4x4 mv = mul(gPassData.view, model);
    float4x4 mvp = mul(gPassData.viewProj, model);
    float4x4 mvpLight = mul(gPassData.viewProjLight, model);

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
    output.instance = instance;
    return output;
}*/

struct MeshVertex
{
    float3 position;
    float4 color;
    float2 uv;
    float3 normal;
    float3 tangent;
    float3 bitangent;
};

struct MSOutput
{
    float4 svPosition : SV_Position;
    [[vk_location(0)]] float3 posView : POSITION0;
    [[vk_location(1)]] float4 color : COLOR0;
    [[vk_location(2)]] float2 uv : TEXCOORD0;
    [[vk_location(3)]] float3 normal : NORMAL0;
    [[vk_location(4)]] float4 posLight : POSITION1;
    [[vk_location(5)]] float3 tbn0 : TBN0;
    [[vk_location(6)]] float3 tbn1 : TBN1;
    [[vk_location(7)]] float3 tbn2 : TBN2;
};

struct PbrMeshData
{
    float4x4 model;
    MeshVertex *vertexData;
    Meshlet *meshletData;
    uint albedoMapIndex;
    uint metallicMapIndex;
    uint roughnessMapIndex;
    uint normalMapIndex;
    uint aoMapIndex;
    uint emissiveMapIndex;
};

[vk_push_constant] ConstantBuffer<PbrMeshData> gMeshData;

bool IsConeCulled(float3 coneAxis, float coneAngle, float3 V)
{
    return dot(coneAxis, V) < coneAngle;
}

// https://gist.github.com/badboy/6267743
uint MeshletColorHash(uint a)
{
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    out vertices MSOutput vtxs[MaxMeshletVertices],
    out indices uint3 idxs[MaxMeshletTriangles],
    uint3 groupId : SV_GroupID,
    uint3 threadId : SV_GroupThreadID)
{
    const uint meshletIdx = groupId.x;
    const uint threadIdx = threadId.x;

    const float4x4 model = gMeshData.model;
    const float4x4 mv = mul(gPassData.view, model);
    const float4x4 mvp = mul(gPassData.viewProj, model);
    const float4x4 mvpLight = mul(gPassData.viewProjLight, model);

    const float3 coneAxis = normalize(mul(mv, float4(gMeshData.meshletData[meshletIdx].coneData.rgb, 0.0)).rgb);
    const float coneAngle = gMeshData.meshletData[meshletIdx].coneData.a;

    if (IsConeCulled(coneAxis, coneAngle, float3(0, 0, 1)))
    {
        SetMeshOutputCounts(0, 0);
        return;
    }

    uint vertexCount = uint(gMeshData.meshletData[meshletIdx].vertexCount);
    uint triangleCount = uint(gMeshData.meshletData[meshletIdx].triangleCount);
    uint indexCount = triangleCount * 3;

    // Copy over vertices, then copy over indices
    for (uint vi = threadIdx; vi < vertexCount; vi += 32)
    {
        MeshVertex v = gMeshData.vertexData[gMeshData.meshletData[meshletIdx].vertices[vi]];
        
        float3 T = normalize(mul(mv, float4(v.tangent, 0.0))).xyz;
        float3 B = normalize(mul(mv, float4(v.bitangent, 0.0))).xyz;
        float3 N = normalize(mul(mv, float4(v.normal, 0.0))).xyz;

        const uint hash = MeshletColorHash(meshletIdx);
        const float4 hashColor = float4(
            (hash & 0xFF) / 255.0,
            ((hash >> 8) & 0xFF) / 255.0,
            ((hash >> 16) & 0xFF) / 255.0,
            1.0
        );

        vtxs[vi].svPosition = mul(mvp, float4(v.position, 1.0));
        vtxs[vi].posView = mul(mv, float4(v.position, 1.0)).xyz;
        vtxs[vi].posLight = mul(mvpLight, float4(v.position, 1.0));
        vtxs[vi].posLight.y *= -1.0f;
        //vtxs[vi].color = IsConeCulled(coneAxis, coneAngle, float3(0, 0, -1)) ? float4(1) : float4(0);
        vtxs[vi].uv = v.uv;
        // vtxs[vi].tbn = transpose(float3x3(T, B, N));
        vtxs[vi].tbn0 = T;
        vtxs[vi].tbn1 = B;
        vtxs[vi].tbn2 = N;
    }

    for (uint i = threadIdx; i < indexCount; i += 32)
    {
        const uint i1 = gMeshData.meshletData[meshletIdx].indices[i];

        idxs[i / 3][i % 3] = i1;
    }

    SetMeshOutputCounts(vertexCount, triangleCount);
}

[vk_binding(1, 0)] ConstantBuffer<LightData> gLightData;
[vk_binding(2, 0)] SamplerCube gIrradianceMap;
[vk_binding(3, 0)] SamplerCube gPrefilterMap;
[vk_binding(4, 0)] Sampler2D gBrdfLut;

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

[shader("fragment")]
float4 FSMain(in MSOutput input) : SV_Target0
{
    float3 albedo = gTextures[gMeshData.albedoMapIndex].Sample(gTrilinearColor, input.uv).rgb;
    float metallic = gTextures[gMeshData.metallicMapIndex].Sample(gTrilinearColor, input.uv).r;
    float roughness = gTextures[gMeshData.roughnessMapIndex].Sample(gTrilinearColor, input.uv).g;
    float ao = gTextures[gMeshData.aoMapIndex].Sample(gTrilinearColor, input.uv).r;

    float3x3 tbn = transpose(float3x3(input.tbn0, input.tbn1, input.tbn2));

    float3 N = gTextures[NonUniformResourceIndex(gMeshData.normalMapIndex)].Sample(gTrilinearColor, input.uv).rgb;
    N = N * 2.0 - 1.0;
    N = normalize(mul(tbn, N));

    float3 V = normalize(-input.posView);

    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

    float3 finalColor = 0;
    for (int i = 0; i < gPassData.lightCount; ++i)
    {
        PointLight l = gLightData.lights[i];

        float3 W = normalize(l.posView - input.posView);
        float3 H = normalize(W + V);

        float lightDistance = distance(gLightData.lights[i].posView, input.posView);

        var radiance = CalculateRadiance(lightDistance, gLightData.lights[i]);

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
    float3 diffuseIrradiance = gIrradianceMap.Sample(N).rgb;
    float3 diffuseAmbient = diffuseIrradiance * albedo;

    static const float maxReflectionLevel = 4.0;
    float3 R = normalize(reflect(-V, N));
    R = mul(gPassData.viewInverse, float4(R, 0.0)).xyz;
    const float3 prefilterColor = gPrefilterMap.SampleLevel(R, roughness * maxReflectionLevel).rgb;
    const float3 F = FresnelSchlickRoughness(F0, V, N, roughness);
    float2 brdfValue = gBrdfLut.Sample(float2(saturate(dot(N, V)), roughness)).rg;
    float3 specular = prefilterColor * (F * brdfValue.x + brdfValue.y);

    float3 ambient = (diffuseAmbient * Kd + specular) * ao;
    float3 emissive = gTextures[gMeshData.emissiveMapIndex].Sample(gTrilinearColor, input.uv).rgb;

    float3 color = ambient + finalColor + emissive;
    color = color / (color + float3(1.0));
    color = pow(color, float3(1.0 / 2.2));
    return float4(color, 1.0);
}