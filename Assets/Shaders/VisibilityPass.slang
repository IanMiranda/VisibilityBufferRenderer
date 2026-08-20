// Shader pipeline for the visibility pass of visibility buffer rendering.
// Only needs to produce the triangle & instance IDs.

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
    uint instance : INSTANCE;
};

struct VbPassData
{
    float4x4 modelViewProj;
    Vertex *vertexData;
};

[vk_push_constant] ConstantBuffer<VbPassData> gPassData;

[shader("vertex")]
VSOutput VSVisibilityPass(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
    Vertex input = gPassData.vertexData[vertex];

    VSOutput output = (VSOutput)0;
    output.svPosition = mul(gPassData.modelViewProj, float4(input.position, 1.0));
    output.instance = instance;
    return output;
}

struct FSOutput
{
    uint2 triangleInstanceIdx;
}

[shader("fragment")]
FSOutput FSVisibilityPass(VSOutput input, uint primitive : SV_PrimitiveID)
{
    FSOutput output = (FSOutput)0;
    output.triangleInstanceIdx.x = primitive;
    output.triangleInstanceIdx.y = input.instance;
    return output;
}
