static const uint TILE_SIZE_X = 16;
static const uint TILE_SIZE_Y = 16;
static const uint PIXELS_PER_TILE = TILE_SIZE_X * TILE_SIZE_Y;
static const uint MAX_SHADERS = 64;

struct Vertex
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct VbShadingData
{
    uint *instanceToShaderIdMap;
    Vertex **vertexBuffers;
    uint **indexBuffers;
    float4x4 *transforms;
    uint *offsetTable;
    uint *tiles;
    uint shaderId;
}

// Gives the barycentric coordinates of point within the triangle.
// Used to compute interpolated values for fragment shading.
// Based on Real-time Rendering, 4th edition (chapter 22)
float3 MollerTrumboreIntersect(float3 p0, float3 p1, float3 p2, float3 o, float3 d)
{
    float3 e1 = p1 - p0;
    float3 e2 = p2 - p0;

    float3 q = cross(d, e2);
    float3 a = dot(e1, q);

    
}

[vk_push_constant] ConstantBuffer<VbShadingData> gShadingData;
[vk_binding(0, 0)] RWTexture2D<uint2> gVisBuf;

[numthreads(TILE_SIZE_X, TILE_SIZE_Y, 1)]
[shader("compute")]
void CSVisibilityShading(uint3 groupId : SV_GroupID, uint3 tileOffset : SV_GroupThreadID)
{
    uint tileId = gShadingData.tiles[gShadingData.offsetTable[gShadingData.shaderId] + groupId.x];
    const uint2 pixelCoord = uint2((tileId >> 16) * TILE_SIZE_X + tileOffset.x,
                                   (tileId & 0xffff) * TILE_SIZE_Y + tileOffset.y);

    uint2 triangleInstanceIdx = gVisBuf.Load(pixelCoord);
    uint triangleIdx = triangleInstanceIdx.x;
    uint instanceIdx = triangleInstanceIdx.y;

    Vertex *vbo = gShadingData.vertexBuffers[instanceIdx];
    uint *ibo = gShadingData.indexBuffers[instanceIdx];
    uint shaderId = gShadingData.instanceToShaderIdMap[instanceIdx];

    if (shaderId != gShadingData.shaderId) return;

    Vertex v0 = vbo[ibo[triangleIdx * 3 + 0]];
    Vertex v1 = vbo[ibo[triangleIdx * 3 + 1]];
    Vertex v2 = vbo[ibo[triangleIdx * 3 + 2]];

    // Now, transform the vertices into their 
}