module Deferred;

import Lighting;

public struct DfMatrixData
{
    public float4x4 view;
    public float4x4 viewProj;
    public float4x4 viewProjLight;
    public float4x4 viewInverse;
};

public static const int gNumLights = 1024;

public struct DfLightData
{
    public PointLight lights[gNumLights];
    public uint lightCount;
};

public struct DfPassData
{
    public ConstantBuffer<DfMatrixData> matrices;
    public ConstantBuffer<DfLightData> lightData;
    public Sampler2D positionMetallic;
    public Sampler2D albedoRoughness;
    public Sampler2D normalAo;
    public Sampler2D emissive;
    public SamplerCube irradianceMap;
    public SamplerCube prefilterMap;
    public Sampler2D brdfLut;
}

public struct DfBindlessData
{
    public SamplerState trilinearColor;
    public Texture2D textures[];
}

