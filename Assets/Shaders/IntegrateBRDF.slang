import Lighting;

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

static const float3 vertices[] =
{
	float3(-1.0, -1.0, 0.0),
	float3(1.0, -1.0, 0.0),
	float3(-1.0, 1.0, 0.0),

	float3(1.0, 1.0, 0.0),
	float3(-1.0, 1.0, 0.0),
	float3(1.0, -1.0, 0.0),
};

static const float2 uvs[] =
{
    float2(0.0, 0.0),
	float2(1.0, 0.0),
	float2(0.0, 1.0),

    float2(1.0, 1.0),
	float2(0.0, 1.0),
	float2(1.0, 0.0),
};

[shader("vertex")]
VSOutput VSMain(uint vertexIndex : SV_VertexID)
{
    VSOutput res = (VSOutput)0;
    res.position = float4(vertices[vertexIndex], 1.0);
    res.uv = uvs[vertexIndex];
	return res;
}

float2 IntegrateBrdf(float NoV, float roughness)
{
    const float3 V = float3(
		sqrt(1.0 - NoV * NoV),
		0.0,
		NoV
	);

    const float3 N = float3(0.0, 0.0, 1.0);

    float scale = 0.0;
    float bias = 0.0;

    static const uint sampleCount = 1024;
    for (uint i = 0; i < sampleCount; ++i)
    {
        float2 Xi = Hammersley(i, sampleCount);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(L.z, 0.0);
        float NoH = max(H.z, 0.0);
        float VoH = saturate(dot(V, H));

        if (NoL > 0.0)
        {
            float G = SmithGGXIbl(roughness, L, V, N);
            float G_Vis = (G * VoH) / (NoH * NoV);
            float Fc = pow(1.0 - VoH, 5.0);

            scale += (1.0 - Fc) * G_Vis;
            bias += Fc * G_Vis;
        }
	}

    return float2(scale / float(sampleCount), bias / float(sampleCount));
}

[shader("fragment")]
float4 FSMain(VSOutput input) : SV_Target0
{
    return float4(IntegrateBrdf(input.uv.x, input.uv.y), 0.0, 1.0);
}