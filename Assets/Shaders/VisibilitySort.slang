static const uint GROUP_SIZE = 256;
static const uint MAX_SHADERS = 64;

struct WorkItem
{
    uint tileId;
    uint shaderId;
}

struct VbSortData
{
    Atomic<uint> *workListCounter;
    uint2 *workList;
    uint *shaderIdToTileCount;
    Atomic<uint> *offsetTable;
    Atomic<uint> *tileBuffer;
    uint4 *indirectBuffer;
    uint2 windowSize;
}

[vk_push_constant] ConstantBuffer<VbSortData> gSortData;

[shader("compute")]
[numthreads(GROUP_SIZE, 1, 1)]
void CSSortWorkList(uint3 globalId : SV_DispatchThreadID)
{
    if (globalId.x == 0)
    {
        gSortData.offsetTable[0].store(0);
        gSortData.offsetTable[1].store(0);
        for (int i = 2; i < MAX_SHADERS + 1; ++i)
        {
            gSortData.offsetTable[i].store(gSortData.offsetTable[i - 1].load() + gSortData.shaderIdToTileCount[i - 2]);
        }
    }

    let workListSize = gSortData.workListCounter.load();

    // Normally this wouldn't be sufficient for a global write,
    // but all our threads are in the same group.
    DeviceMemoryBarrierWithGroupSync();

    uint localId = globalId.x;

    while (localId < workListSize)
    {
        uint workItemIdx = gSortData.workListCounter.decrement();

        uint2 item = gSortData.workList[workItemIdx - 1];
        uint tileBufferIdx = gSortData.offsetTable[item.y + 1].increment();
        gSortData.tileBuffer[tileBufferIdx] = item.x;

        localId += GROUP_SIZE;
    }

    DeviceMemoryBarrierWithGroupSync();

    if (globalId.x == 0) {
        for (int i = 1; i < MAX_SHADERS + 1; ++i) {
            gSortData.indirectBuffer[i] = uint4(gSortData.shaderIdToTileCount[i], 1, 1, 1);
        }
    }
    
}