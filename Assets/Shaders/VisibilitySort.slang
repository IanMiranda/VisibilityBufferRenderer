static const uint GROUP_SIZE = 256;
static const uint MAX_SHADERS = 64;

struct WorkItem
{
    uint tileId;
    uint shaderId;
}

struct VbSortData
{
    uint *instanceToShaderIdMap;
    uint *workListCounter;
    WorkItem *workList;
    uint *shaderIdToTileCount;
    uint *offsetTable;
    uint2 windowSize;
    uint workListSize;
}

[vk_push_constant] ConstantBuffer<VbSortData> gSortData;

[numthreads(GROUP_SIZE, 1, 1)]
[shader("compute")]
void CSSortWorklist(uint3 globalId : SV_DispatchThreadID)
{
    if (globalId.x == 0)
    {
        gSortData.offsetTable[0] = 0;
        gSortData.offsetTable[1] = 0;
        for (int i = 2; i < MAX_SHADERS + 1; ++i)
        {
            gSortData.offsetTable[i] = gSortData.offsetTable[i - 1] + gSortData.shaderIdToTileCount[i - 1];
        }
    }

    DeviceMemoryBarrierWithGroupSync();

    uint localId = globalId.x;

    while (localId < gSortData.workListSize)
    {
        
    }
}