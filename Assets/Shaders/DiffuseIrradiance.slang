import Math;

struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 localPos : POSITION;
};

struct EqMapData
{
	float4x4 viewProj;
};

[push_constant] ConstantBuffer<EqMapData> gEqMapData;

[shader("vertex")]
VSOutput VSMain(float3 pos : POSITION)
{
	VSOutput res = (VSOutput)0;
    res.pos = mul(gEqMapData.viewProj, float4(pos, 1.0)).xyww;
    res.localPos = pos;
	return res;
}

[vk_binding(0, 0)] SamplerCube gEnvMap;

[shader("fragment")]
float4 FSMain(VSOutput input) : SV_Target0
{
    float3 N = normalize(input.localPos);
    const float3 worldUp = float3(0.0, 1.0, 0.0);
    const float3 right = normalize(cross(worldUp, N));
    const float3 up = normalize(cross(N, right));

    float3 irradiance = float3(0.0);

    const float sampleDelta = 0.1;
    float samples = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            float3 tangentSample = float3(
                sin(theta) * cos(phi),
                sin(theta) * sin(phi),
                cos(theta)
            );
            float3 sampleDir = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
            irradiance += gEnvMap.Sample(sampleDir).rgb * cos(theta) * sin(theta);
            samples += 1.0;
        }
    }

    irradiance = PI * irradiance * (1.0 / samples);
    return float4(irradiance, 1.0);
}