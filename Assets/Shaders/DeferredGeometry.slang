import Deferred;
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
    float3x3 tbn : TBN;
};

struct DfObjectData
{
    float4x4 modelView;
    float4x4 modelViewProj;
    Vertex *vertexBuffer;
    uint albedoMapIndex;
    uint metallicRoughnessMapIndex;
    uint normalMapIndex;
    uint aoMapIndex;
    uint emissiveMapIndex;
};

ParameterBlock<DfBindlessData> gBindless;

[shader("vertex")]
VSOutput VSGeometryPass(
    uniform DfObjectData object,
    uint vertex : SV_VulkanVertexID)
{
    VSOutput output = (VSOutput)0;
    Vertex input = object.vertexBuffer[vertex];

    let mv = object.modelView;
    let mvp = object.modelViewProj;

    float3 T = normalize(mul(mv, float4(input.tangent, 0.0))).xyz;
    float3 B = normalize(mul(mv, float4(input.bitangent, 0.0))).xyz;
    float3 N = normalize(mul(mv, float4(input.normal, 0.0))).xyz;
    output.tbn = transpose(float3x3(T, B, N));

    output.svPosition = mul(mvp, float4(input.position, 1.0));
    output.posView = mul(mv, float4(input.position, 1.0)).xyz;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float4 SampleMaterialTexture(uint index, float2 uv)
{
    return gBindless.textures[NonUniformResourceIndex(index)].Sample(gBindless.trilinearColor, uv);
}

struct FSOutput
{
    float4 positionMetallic : SV_Target0;
    float4 albedoRoughness : SV_Target1;
    float4 normalAo : SV_Target2;
    float4 emissive : SV_Target3;
}

[shader("fragment")]
FSOutput FSGeometryPass(
    uniform DfObjectData object,
    VSOutput input)
{
    float3 albedo = SampleMaterialTexture(object.albedoMapIndex, input.uv).rgb;
    float metallic = SampleMaterialTexture(object.metallicRoughnessMapIndex, input.uv).r;
    float roughness = SampleMaterialTexture(object.metallicRoughnessMapIndex, input.uv).g;
    float ao = SampleMaterialTexture(object.aoMapIndex, input.uv).r;

    float3 N = SampleMaterialTexture(object.normalMapIndex, input.uv).rgb;
    N = N * 2.0 - 1.0;
    N = normalize(mul(input.tbn, N));
    float3 emissive = SampleMaterialTexture(object.emissiveMapIndex, input.uv).rgb;

    FSOutput output = (FSOutput)0;
    output.positionMetallic.xyz = input.posView;
    output.positionMetallic.w = metallic;
    output.albedoRoughness.xyz = albedo;
    output.albedoRoughness.w = roughness;
    output.normalAo.xyz = N;
    output.normalAo.w = ao;
    output.emissive = float4(emissive, 0.0);
    return output;
}