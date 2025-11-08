module Common;

public static const int gNumLights = 1024;

public struct ObjectData
{
    public float4x4 model;
    public uint textureIndex;
    public uint normalMapIndex;
};

public struct GlobalPassData
{
    public float4x4 view;
    public float4x4 viewProj;
    public float4x4 viewProjLight;
    public float4x4 viewInverse;
    public uint lightCount;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

public struct PointLight
{
    public float3 posView;
    float _pad0;
    public float4 i;
};

public struct LightData
{
    public PointLight lights[gNumLights];
};

[push_constant] public ConstantBuffer<ObjectData> gObjectData;
[vk_binding(0, 0)] public ConstantBuffer<GlobalPassData> gPassData;
[vk_binding(1, 0)] public ConstantBuffer<LightData> gLightData;

[vk_binding(2, 0)] public SamplerCube gCubemap;
// [vk_binding(3, 0)] public Sampler2DShadow gShadowMap;

[vk_binding(0, 1)] public Sampler2D gTextures[];