module Mesh;

public static const uint MaxMeshletVertices = 64;
public static const uint MaxMeshletTriangles = 126;
public static const uint MaxMeshletIndices = MaxMeshletTriangles * 3;

public struct Meshlet
{
    public float4 coneData;
    public uint vertices[MaxMeshletVertices];
    public uint8_t indices[MaxMeshletIndices];
    public uint8_t vertexCount;
    public uint8_t triangleCount;
};