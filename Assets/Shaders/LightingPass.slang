import Lighting;

struct VSOutput
{
    float4 svPosition : SV_Position;
    float2 uv : TEXCOORD;
}

[shader("vertex")]
VSOutput VSMain(uint vertexIndex: SV_VertexID)
{
    VSOutput output = (VSOutput)0;
    output.svPosition = float4(-1.0 + ((vertexIndex & 0x2) * 4.0), -1.0 + (vertexIndex % 2) * 4.0, 0.01, 1.0);
    output.uv = float2((vertexIndex == 2) * 4, 2 - (vertexIndex == 1) * 2);
    return output;
}

struct LightingPassData
{
    uint lightCount;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

[push_constant] ConstantBuffer<LightingPassData> gPassData;
[vk_binding(0, 0)] ConstantBuffer<LightData> gLightData;
[vk_binding(1, 0)] Sampler2D gPositionMap;
[vk_binding(2, 0)] Sampler2D gNormalMap;
[vk_binding(3, 0)] Sampler2D gAlbedoMap;
[vk_binding(4, 0)] Sampler2D gSpecularMap;

float4 Diffuse(float3 N, float3 W, float4 Kd, float4 I, float dist)
{
    float NoW = dot(N, W);

    // Attenuation
    I *= (1.0 / (dist * dist + 0.01));
    float4 diffuse = saturate(NoW) * Kd * I;
    return diffuse;
}

float4 Specular(float3 N, float3 W, float3 V, float4 Ks, float4 I, float alpha)
{
    float3 H = normalize(W + V);
    // float3 R = normalize(reflect(W, N));
    float NoH = dot(N, H);
    // float VoR = dot(V, R);
    float4 specular = I * Ks * pow(saturate(NoH), alpha);
    // float4 specular = I * Ks * pow(max(VoR, 0.0), alpha);
    return specular;
}

[shader("fragment")]
float4 FSMain(VSOutput input) : SV_Target0
{
    float2 pos = input.uv;
    // Convert pos to sampling coords
    float alpha = 16.0;

    float4 Ia = float4(0.01, 0.01, 0.01, 1.0);

    float4 Kd = gAlbedoMap.Sample(pos.xy);
    float4 Ks = gSpecularMap.Sample(pos.xy);

    float3 N = gNormalMap.Sample(pos.xy).rgb;

    float3 posView = gPositionMap.Sample(pos.xy).xyz;

    float4 finalColor = 0;
    for (int i = 0; i < gPassData.lightCount; ++i)
    {
        PointLight l = gLightData.lights[i];
        float3 W = normalize(l.posView - posView);
        float4 diffuse = Diffuse(N, W, Kd, l.i, distance(gLightData.lights[i].posView, posView));

        float3 V = -normalize(posView);
        float4 specular = Specular(N, W, V, Ks, l.i, alpha);

        float4 ambient = Kd * Ia;

        // Environment reflections
        /*float3 Wr = normalize(reflect(-V, N));
        float4 Kr = Ks * 0.1;
        float4 envColor = Kr * gCubemap.Sample(mul(gPassData.viewInverse, float4(Wr, 1.0)).xyz);*/

        // float recordedDepth = GetRecordedDepth(input.posLight);
        finalColor += ambient + (diffuse + specular/* + envColor*/); // * recordedDepth;
    }

    return finalColor;
}