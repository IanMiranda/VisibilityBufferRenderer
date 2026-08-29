static const uint TILE_SIZE_X = 16;
static const uint TILE_SIZE_Y = 16;
static const uint PIXELS_PER_TILE = TILE_SIZE_X * TILE_SIZE_Y;
static const uint MAX_SHADERS = 64;

struct Vertex
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct VbMaterialData
{
    uint albedoMapIndex;
    uint metallicMapIndex;
    uint roughnessMapIndex;
    uint normalMapIndex;
    uint aoMapIndex;
    uint emissiveMapIndex;
};

struct VbShadingData
{
    uint *instanceToShaderIdMap;
    Vertex **vertexBuffers;
    uint **indexBuffers;
    float4x4 *transforms;
    Atomic<uint> *offsetTable;
    Atomic<uint> *tiles;
    VbMaterialData *materials;
    uint2 screenSize;
    uint shaderId;
}

static const int gNumLights = 1024;

import Lighting;

struct VbConstantData
{
    float4x4 view;
    float4x4 viewProj;
    float4x4 viewProjLight;
    float4x4 viewInverse;
}

struct VbPushDescData
{
    RWTexture2D<uint2> visBuf;
    RWTexture2D<float4> frameBuf;
    ConstantBuffer<VbConstantData> constants;
}

struct VbBindlessData
{
    SamplerState trilinearColor;
    Texture2D textures[];
}

struct VbLightData
{
    PointLight lights[gNumLights];
    uint lightCount;
}

struct VbLightingData
{
    ConstantBuffer<VbLightData> data;
    SamplerCube irradianceMap;
    SamplerCube prefilterMap;
    Sampler2D brdfLut;
}

[vk_binding(0, 0)] ParameterBlock<VbPushDescData> gPushDesc;
[vk_binding(0, 1)] ParameterBlock<VbLightingData> gLighting;
[vk_binding(0, 2)] ParameterBlock<VbBindlessData> gBindless;

// Edge function for computing barycentrics
float EdgeFunction(float2 pixelCoord, float2 edgeNormal, float2 pointOnEdge)
{
    return dot(edgeNormal, pixelCoord - pointOnEdge);
}

float2 rotate90Ccw(float2 toRotate)
{
    return float2(-toRotate.y, toRotate.x);
}

float2 toScreenSpace(float4 clipSpace, uint2 screenSize)
{
    return float2(
        (((clipSpace.x / clipSpace.w) + 1) * screenSize.x) / 2,
        (((clipSpace.y / clipSpace.w) + 1) * screenSize.y) / 2);
}

// Perspective-correct interpolation for vertices, as discussed in
// Real-time Rendering Ch. 23
float3 ComputeBarycentrics(float2 pixelCoord, float4 p0, float4 p1, float4 p2, uint2 screenSize, bool print)
{
    let s0 = toScreenSpace(p0, screenSize);
    let s1 = toScreenSpace(p1, screenSize);
    let s2 = toScreenSpace(p2, screenSize);

    float e0 = EdgeFunction(pixelCoord, rotate90Ccw(s2 - s1), s1);
    float e1 = EdgeFunction(pixelCoord, rotate90Ccw(s0 - s2), s2);
    float e2 = EdgeFunction(pixelCoord, rotate90Ccw(s1 - s0), s0);

    float f0 = e0 / p0.w;
    float f1 = e1 / p1.w;
    float f2 = e2 / p2.w;

    return float3(f0, f1, f2) * rcp(f0 + f1 + f2);
}

vector<float, N> InterpolateQuantity<let N: int>(
    float3 barys,
    vector<float, N> v0,
    vector<float, N> v1,
    vector<float, N> v2)
{
    return v0 * barys.x + v1 * barys.y + v2 * barys.z;
}

Vertex InterpolateVertex(float3 barys, Vertex v0, Vertex v1, Vertex v2)
{
    Vertex res = (Vertex)0;
    res.color = InterpolateQuantity(barys, v0.color, v1.color, v2.color);
    res.uv = InterpolateQuantity(barys, v0.uv, v1.uv, v2.uv);
    res.normal = InterpolateQuantity(barys, v0.normal, v1.normal, v2.normal);
    res.tangent = InterpolateQuantity(barys, v0.tangent, v1.tangent, v2.tangent);
    res.bitangent = InterpolateQuantity(barys, v0.bitangent, v1.bitangent, v2.bitangent);
    return res;
}

float4 SampleMaterialTexture(uint index, float2 uv)
{
    return gBindless.textures[NonUniformResourceIndex(index)].SampleLevel(gBindless.trilinearColor, uv, 0);
}

float3 CalculateRadiance(float distance, PointLight light)
{
    float attenuation = 1.0 / (distance * distance);
    return light.i.rgb * attenuation;
}

float3 FresnelSchlick(float3 F0, float3 W, float3 H)
{
    return F0 + (1 - F0) * pow(1 - saturate(dot(W, H)), 5.0);
}

float3 FresnelSchlickRoughness(float3 F0, float3 W, float3 H, float roughness)
{
    return F0 + (max(float3(1 - roughness), F0) - F0) * pow(1 - saturate(dot(W, H)), 5.0);
}


import Math;

[numthreads(TILE_SIZE_X, TILE_SIZE_Y, 1)]
[shader("compute")]
void CSVisibilityShading(
    uniform VbShadingData shadingData,
    uint3 groupId : SV_GroupID,
    uint3 tileOffset : SV_GroupThreadID)
{
    uint tileId = shadingData.tiles[shadingData.offsetTable[shadingData.shaderId].load() + groupId.x].load();

    let pixelCoord = uint2((tileId & 0xffff) * TILE_SIZE_X + tileOffset.x,
                                   (tileId >> 16) * TILE_SIZE_Y + tileOffset.y);

    // Fetch relevant geometry info
    uint2 triangleInstanceIdx = gPushDesc.visBuf.Load(pixelCoord);
    uint triangleIdx = triangleInstanceIdx.x;
    uint instanceIdx = triangleInstanceIdx.y;

    if (instanceIdx == 0) return;

    Vertex *vbo = shadingData.vertexBuffers[instanceIdx];
    uint *ibo = shadingData.indexBuffers[instanceIdx];
    uint shaderId = shadingData.instanceToShaderIdMap[instanceIdx];

    if (shaderId != shadingData.shaderId) return;

    Vertex v0 = vbo[ibo[triangleIdx * 3 + 0]];
    Vertex v1 = vbo[ibo[triangleIdx * 3 + 1]];
    Vertex v2 = vbo[ibo[triangleIdx * 3 + 2]];

    // Now, transform the vertices into their clip-space coords

    let model = shadingData.transforms[instanceIdx];
    let mv = mul(gPushDesc.constants.view, model);
    let mvp = mul(gPushDesc.constants.viewProj, model);
    
    let p0 = mul(mvp, float4(v0.position, 1.0));
    let p1 = mul(mvp, float4(v1.position, 1.0));
    let p2 = mul(mvp, float4(v2.position, 1.0));

    // printf("o0 = (%v3f), o1 = (%v3f), o2 = (%v3f)", v0.position, v1.position, v2.position);

    // Finally, compute bary coords and interpolate
    // printf("instance: %d, triangle: %d, pix: %d", instanceIdx, triangleIdx, pixelCoord);
    let realPixelCoord = float2(pixelCoord.x + 0.5, shadingData.screenSize.y - pixelCoord.y + 0.5);
    let barys = ComputeBarycentrics(realPixelCoord, p0, p1, p2, shadingData.screenSize, true);

    let input = InterpolateVertex(barys, v0, v1, v2);
    
    // Recreate TBN matrix
    float3 t = normalize(mul(mv, float4(input.tangent, 0.0))).xyz;
    float3 b = normalize(mul(mv, float4(input.bitangent, 0.0))).xyz;
    float3 n = normalize(mul(mv, float4(input.normal, 0.0))).xyz;
    let tbn = transpose(float3x3(t, b, n));

    let material = shadingData.materials[instanceIdx];
    float3 albedo = SampleMaterialTexture(material.albedoMapIndex, input.uv).rgb;
    float metallic = SampleMaterialTexture(material.metallicMapIndex, input.uv).r;
    float roughness = SampleMaterialTexture(material.roughnessMapIndex, input.uv).g;
    float ao = SampleMaterialTexture(material.aoMapIndex, input.uv).r;

    float3 N = SampleMaterialTexture(material.normalMapIndex, input.uv).rgb;
    N = N * 2.0 - 1.0;
    N = normalize(mul(tbn, N));

    let posView = InterpolateQuantity(
        barys,
        mul(mv, float4(v0.position, 1.0)).xyz,
        mul(mv, float4(v1.position, 1.0)).xyz,
        mul(mv, float4(v2.position, 1.0)).xyz
    );

    float3 V = normalize(-posView);

    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

    float3 finalColor = 0;
    for (int i = 0; i < gLighting.data.lightCount; ++i)
    {
        PointLight l = gLighting.data.lights[i];

        float3 W = normalize(l.posView - posView);
        float3 H = normalize(W + V);

        float lightDistance = distance(gLighting.data.lights[i].posView, posView);

        var radiance = CalculateRadiance(lightDistance, gLighting.data.lights[i]);

        float3 F = FresnelSchlick(F0, W, H);

        float D = TrowbridgeReitzGGX(roughness * roughness, N, H);
        float G = SmithGGX(roughness, W, V, N);
        float3 num = D * G * F;
        float denom = 4.0 * saturate(dot(N, V)) * saturate(dot(N, W)) + 0.0001;

        float3 specularTerm = num / denom;
        float3 Ks = F;
        float3 Kd = float3(1.0) - Ks;
        Kd *= 1.0 - metallic;

        float NoW = saturate(dot(N, W));
        finalColor += (Kd * albedo / PI + specularTerm) * radiance * NoW;
    }

    float3 Ks = FresnelSchlickRoughness(F0, N, V, roughness);
    float3 Kd = float3(1.0) - Ks;
    float3 diffuseIrradiance = gLighting.irradianceMap.SampleLevel(N, 0).rgb; // NOTE: manual
    float3 diffuseAmbient = diffuseIrradiance * albedo;

    static const float maxReflectionLevel = 4.0;
    float3 R = normalize(reflect(-V, N));
    R = mul(gPushDesc.constants.viewInverse, float4(R, 0.0)).xyz;
    const float3 prefilterColor = gLighting.prefilterMap.SampleLevel(R, roughness * maxReflectionLevel).rgb;
    const float3 F = FresnelSchlickRoughness(F0, V, N, roughness);
    float2 brdfValue = gLighting.brdfLut.SampleLevel(float2(saturate(dot(N, V)), roughness), 0).rg; // TODO: manual
    float3 specular = prefilterColor * (F * brdfValue.x + brdfValue.y);

    float3 ambient = (diffuseAmbient * Kd + specular) * ao;
    float3 emissive = SampleMaterialTexture(material.emissiveMapIndex, input.uv).rgb;

    float3 color = ambient + finalColor + emissive;
    color = color / (color + float3(1.0));
    color = pow(color, float3(1.0 / 2.2));

    gPushDesc.frameBuf.Store(pixelCoord, float4(color, 1.0));
}