static const uint TILE_SIZE_X = 16;
static const uint TILE_SIZE_Y = 16;
static const uint PIXELS_PER_TILE = TILE_SIZE_X * TILE_SIZE_Y;
static const uint MAX_SHADERS = 64;

struct VbWorkItem
{
    uint tileId;
    uint shaderId;
}

struct VbBuildData
{
    uint *instanceToShaderIdMap;
    Atomic<uint> *workListCounter;
    uint2 *workList;
    Atomic<uint> *shaderIdToTileCount;
    uint2 windowSize;
}

[vk_push_constant] VbBuildData gBuildData;

// Though we only read from this, Slang forces us to mark it
// RW so we can use load and add its descriptor binding as
// a storage image.
[vk_binding(0, 0)] RWTexture2D<uint2> gVisBuf;

groupshared Atomic<uint> gShaderActiveInTile[MAX_SHADERS];

[shader("compute")]
[numthreads(TILE_SIZE_X, TILE_SIZE_Y, 1)]
void CSBuildWorklist(
    uint3 localId : SV_GroupThreadID,
    uint3 globalId : SV_DispatchThreadID,
    uint localIndex : SV_GroupIndex)
{
    let pixelId = globalId.xy;

    if (localIndex == 0)
    {
        for (uint i = 0; i < MAX_SHADERS; ++i)
        {
            gShaderActiveInTile[i].store(0);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const uint2 triangleInstanceIdx = gVisBuf.Load(pixelId);
    gShaderActiveInTile[gBuildData.instanceToShaderIdMap[triangleInstanceIdx.y]].store(1);

    GroupMemoryBarrierWithGroupSync();
    
    const uint tileId = ((pixelId.y / TILE_SIZE_Y) << 16) | (pixelId.x / TILE_SIZE_X);
    uint currentShaderId = localIndex;
    while (currentShaderId < MAX_SHADERS)
    {
        if (gShaderActiveInTile[currentShaderId].load() > 0)
        {
            // Increment the worklist size and move our data into it
            let itemIndex = gBuildData.workListCounter->increment();
            gBuildData.workList[itemIndex].x = tileId;
            gBuildData.workList[itemIndex].y = currentShaderId;
            gBuildData.shaderIdToTileCount[currentShaderId].increment();
        }

        currentShaderId += PIXELS_PER_TILE;
    }
}