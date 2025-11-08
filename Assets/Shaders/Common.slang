module Common;

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
    public float3 lightDir;
    float _pad0;
};

[push_constant] public ConstantBuffer<ObjectData> gObjectData;
[vk_binding(0, 0)] public ConstantBuffer<GlobalPassData> gPassData;

[vk_binding(1, 0)] public SamplerCube gCubemap;
[vk_binding(2, 0)] public Sampler2DShadow gShadowMap;

[vk_binding(0, 1)] public Sampler2D gTextures[];