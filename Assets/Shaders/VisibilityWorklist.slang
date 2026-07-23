static const uint TILE_SIZE_X = 16;
static const uint TILE_SIZE_Y = 16;
static const uint PIXELS_PER_TILE = TILE_SIZE_X * TILE_SIZE_Y;
static const uint MAX_SHADERS = 64;

struct WorkItem
{
    uint tileId;
    uint shaderId;
}

struct VbBuildData
{
    uint *instanceToShaderIdMap;
    uint *workListCounter;
    WorkItem *workList;
    uint *shaderIdToTileCount;
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
    
    if (pixelId.x < gBuildData.windowSize.x && pixelId.y < gBuildData.windowSize.y)
    {
        if (localIndex == 0)
        {
            for (uint i = 0; i < MAX_SHADERS; ++i)
            {
                shaderActiveInTile[i] = 0;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (pixelId.x < gBuildData.windowSize.x && pixelId.y < gBuildData.windowSize.y)
    {
        const uint2 triangleInstanceIdx = gVisBuf.Load(int3(pixelId, 0));
        shaderActiveInTile[gBuildData.instanceToShaderIdMap[triangleInstanceIdx.y]] = 1;
    }

    GroupMemoryBarrierWithGroupSync();

    if (pixelId.x < gBuildData.windowSize.x && pixelId.y < gBuildData.windowSize.y)
    {
        const uint tileId = ((pixelId.x / TILE_SIZE_X) << 16) | (pixelId.y / TILE_SIZE_Y);
        uint currentShaderId = localIndex;
        while (currentShaderId < MAX_SHADERS)
        {
            if (shaderActiveInTile[currentShaderId] == 1)
            {
                uint originalTilesActive;

                // Increment the worklist size and move our data into it
                uint itemIndex;
                InterlockedAdd(*gBuildData.workListCounter, 1, itemIndex);
                gBuildData.workList[itemIndex].shaderId = currentShaderId;
                gBuildData.workList[itemIndex].tileId = tileId;
            }

            currentShaderId += PIXELS_PER_TILE;
        }
    }
}
