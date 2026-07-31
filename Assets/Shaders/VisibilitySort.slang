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
    WorkItem *workList;
    Atomic<uint> *shaderIdToTileCount;
    Atomic<uint> *offsetTable;
    uint *tileBuffer;
    uint2 windowSize;
}

[vk_push_constant] ConstantBuffer<VbSortData> gSortData;

[numthreads(GROUP_SIZE, 1, 1)]
[shader("compute")]
void CSSortWorkList(uint3 globalId : SV_DispatchThreadID)
{
    if (globalId.x == 0)
    {
        gSortData.offsetTable[0] = 0;
        gSortData.offsetTable[1] = 0;
        for (int i = 2; i < MAX_SHADERS + 1; ++i)
        {
            gSortData.offsetTable[i].store(gSortData.offsetTable[i - 1].add(gSortData.shaderIdToTileCount[i - 1].load()));
        }
    }

    let workListSize = gSortData.workListCounter.load();
    DeviceMemoryBarrierWithGroupSync();

    uint localId = globalId.x;

    while (localId < workListSize)
    {
        uint workItemIdx = gSortData.workListCounter.decrement();

        WorkItem item = gSortData.workList[workItemIdx - 1];
        uint tileBufferIdx = gSortData.offsetTable[item.shaderId + 1].increment();
        gSortData.tileBuffer[tileBufferIdx] = item.tileId;

        localId += GROUP_SIZE;
    }
}