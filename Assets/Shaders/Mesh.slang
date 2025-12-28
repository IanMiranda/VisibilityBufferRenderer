module Mesh;

public static const uint MaxMeshletVertices = 64;
public static const uint MaxMeshletTriangles = 126;
public static const uint MaxMeshletIndices = MaxMeshletTriangles * 3;

public struct Meshlet
{
    public uint vertices[MaxMeshletVertices];
    public uint indices[MaxMeshletIndices];
    public uint vertexCount;
    public uint triangleCount;
};