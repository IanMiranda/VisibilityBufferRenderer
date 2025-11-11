module Common;

public struct ObjectData
{
    public float4x4 model;
    public uint diffuseMapIndex;
    public uint specularMapIndex;
    public uint normalMapIndex;
};

[vk_binding(0, 1)] public Sampler2D gTextures[];