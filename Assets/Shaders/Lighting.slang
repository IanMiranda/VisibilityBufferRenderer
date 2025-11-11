module Lighting;

public static const int gNumLights = 1024;

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

