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

struct VbMaterialData
{
    uint albedoMapIndex;
};

struct VbShadingData
{
    uint *instanceToShaderIdMap;
    Vertex **vertexBuffers;
    uint **indexBuffers;
    float4x4 *transforms;
    Atomic<uint> *offsetTable;
    Atomic<uint> *tiles;
    VbMaterialData *materials;
    uint2 screenSize;
    uint shaderId;
}

[vk_binding(0, 0)] RWTexture2D<uint2> gVisBuf;
[vk_binding(1, 0)] RWTexture2D<float4> gFrameBuf;
[vk_binding(0, 1)] SamplerState gTrilinearColor;
[vk_binding(1, 1)] Texture2D gTextures[];

// Edge function for computing barycentrics
float EdgeFunction(float2 pixelCoord, float2 edgeNormal, float2 pointOnEdge)
{
    return dot(edgeNormal, pixelCoord - pointOnEdge);
}

float2 rotate90Ccw(float2 toRotate)
{
    return float2(-toRotate.y, toRotate.x);
}

// Perspective-correct interpolation for vertices, as discussed in
// Real-time Rendering Ch. 23
float3 ComputeBarycentrics(float2 pixelCoord, float4 p0, float4 p1, float4 p2)
{
    let p0xy = p0.xy / p0.w;
    let p1xy = p1.xy / p1.w;
    let p2xy = p2.xy / p2.w;
    
    float e0 = EdgeFunction(pixelCoord, rotate90Ccw(p2xy - p1xy), p1xy);
    float e1 = EdgeFunction(pixelCoord, rotate90Ccw(p0xy - p2xy), p2xy);
    float e2 = EdgeFunction(pixelCoord, rotate90Ccw(p1xy - p0xy), p0xy);

    float f0 = e0 * p0.w;
    float f1 = e1 * p1.w;
    float f2 = e2 * p2.w;

    return float3(f0, f1, f2) * rcp(f0 + f1 + f2);
}

[numthreads(TILE_SIZE_X, TILE_SIZE_Y, 1)]
[shader("compute")]
void CSVisibilityShading(uniform VbShadingData gShadingData, uint3 groupId : SV_GroupID, uint3 tileOffset : SV_GroupThreadID)
{
    uint tileId = gShadingData.tiles[gShadingData.offsetTable[gShadingData.shaderId].load() + groupId.x].load();

    let pixelCoord = uint2((tileId & 0xffff) * TILE_SIZE_X + tileOffset.x,
                                   (tileId >> 16) * TILE_SIZE_Y + tileOffset.y);
    let pixelClipSpace = ((pixelCoord + float2(0.5, 0.5)) / gShadingData.screenSize * 2.0) - 1.0;

    // Fetch relevant geometry info
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

    // Now, transform the vertices into their clip-space coords
    let p0 = mul(gShadingData.transforms[instanceIdx], float4(v0.position, 1.0));
    let p1 = mul(gShadingData.transforms[instanceIdx], float4(v1.position, 1.0));
    let p2 = mul(gShadingData.transforms[instanceIdx], float4(v2.position, 1.0));

    // Finally, compute bary coords and interpolate
    let barys = ComputeBarycentrics(pixelClipSpace, p0, p1, p2);

    // Use this to sample from the appropriate material
    float2 uv = float2(
        v0.uv.x * barys.x + v1.uv.x * barys.y + v2.uv.x * barys.z,
        v0.uv.y * barys.x + v1.uv.y * barys.y + v2.uv.y * barys.z,
    );

    let albedo = gTextures[gShadingData.materials[instanceIdx].albedoMapIndex].SampleLevel(gTrilinearColor, uv, 0);
    gFrameBuf.Store(pixelCoord, albedo);
}