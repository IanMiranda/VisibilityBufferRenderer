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
VSOutput VSMain(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
    Vertex input = gPassData.vertexData[vertex];

    VSOutput output = (VSOutput)0;
    output.svPosition = mul(gPassData.modelViewProj, float4(input.position, 1.0));
    output.instance = instance;
    return output;
}

struct FSOutput
{
    uint32_t2 triangleInstanceIdx;
}

[shader("fragment")]
FSOutput FSMain(VSOutput input, uint triangleIdx : SV_PrimitiveID)
{
    FSOutput output = (FSOutput)0;
    output.triangleInstanceIdx.x = triangleIdx;
    output.triangleInstanceIdx.y = input.instance;
    return output;
}

static const uint TILE_SIZE_X = 16;
static const uint TILE_SIZE_Y = 16;
static const uint MAX_SHADERS = 64;

struct VbBuildData
{
    uint *instanceToShaderIdMap;
    uint2 windowSize;
}

[vk_push_constant] VbBuildData gBuildData;
[vk_binding(0, 0)] Texture2D<uint2> gVisBuf;

groupshared uint shaderActiveInTile[MAX_SHADERS];

[shader("compute")]
[numthreads(TILE_SIZE_X, TILE_SIZE_Y)]
void CSBuildWorklist(uint3 localId : SV_GroupThreadID, uint3 globalId : SV_DispatchThreadID, uint localIndex : SV_GroupIndex)
{
    const uint2 pixelId = globalId.xy;
    
    if (pixelId.x < gBuildData.windowSize.x || pixelId.y < gBuildData.windowSize.y) {
        if (localIndex == 0) {
            for (uint i = 0; i < MAX_SHADERS; ++i) {
                shaderActiveInTile[i] = 0;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (pixelId.x < gBuildData.windowSize.x || pixelId.y < gBuildData.windowSize.y) {
        const uint2 triangleInstanceIdx = gVisBuf.Load((pixelId, 0));
        shaderActiveInTile[gBuildData.instanceToShaderIdMap[triangleInstanceIdx.y]] = 1;
    }

    GroupMemoryBarrierWithGroupSync();

    
}
