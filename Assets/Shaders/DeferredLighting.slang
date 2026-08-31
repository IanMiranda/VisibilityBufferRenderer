import Lighting;
import Deferred;
import Math;

ParameterBlock<DfPassData> gPass;

struct VSOutput
{
    float4 svPosition : SV_POSITION;
    float2 uv : TEXCOORD0;
}

[shader("vertex")]
VSOutput VSLightingPass(
    uint vertex : SV_VulkanVertexID)
{
    // https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
    // Vertex 0: -1, -1 (uv 0, 0)
    // Vertex 1: 3, -1 (uv 2, 0)
    // Vertex 2: -1, 3 (uv 0, 2)

    // Need to flip again, since using negative viewport height
    VSOutput output = (VSOutput)0;
    output.uv = float2((vertex << 1) & 0x2, (vertex & 0x2));
    output.svPosition = float4(output.uv * 2.0 - 1.0f, 0.0f, 1.0f);
    output.uv.y *= -1.0f;
    return output;
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

[shader("fragment")]
float4 FSLightingPass(
    VSOutput input) : SV_Target0
{
    let positionMetallic = gPass.positionMetallic.Sample(input.uv);
    let albedoRoughness = gPass.albedoRoughness.Sample(input.uv);
    let normalAo = gPass.normalAo.Sample(input.uv);

    let albedo = albedoRoughness.rgb;
    float metallic = positionMetallic.a;
    float roughness = albedoRoughness.a;
    float ao = normalAo.w;

    let N = normalAo.xyz;

    let posView = positionMetallic.xyz;
    float3 V = normalize(-posView);

    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

    float3 finalColor = 0;
    for (int i = 0; i < gPass.lightData.lightCount; ++i)
    {
        PointLight l = gPass.lightData.lights[i];

        float3 W = normalize(l.posView - posView);
        float3 H = normalize(W + V);

        float lightDistance = distance(gPass.lightData.lights[i].posView, posView);

        var radiance = CalculateRadiance(lightDistance, gPass.lightData.lights[i]);

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
    float3 diffuseIrradiance = gPass.irradianceMap.Sample(N).rgb;
    float3 diffuseAmbient = diffuseIrradiance * albedo;

    static const float maxReflectionLevel = 4.0;
    float3 R = normalize(reflect(-V, N));
    R = mul(gPass.matrices.viewInverse, float4(R, 0.0)).xyz;
    const float3 prefilterColor = gPass.prefilterMap.SampleLevel(R, roughness * maxReflectionLevel).rgb;
    const float3 F = FresnelSchlickRoughness(F0, V, N, roughness);
    float2 brdfValue = gPass.brdfLut.Sample(float2(saturate(dot(N, V)), roughness)).rg;
    float3 specular = prefilterColor * (F * brdfValue.x + brdfValue.y);

    float3 ambient = (diffuseAmbient * Kd + specular) * ao;
    float3 emissive = gPass.emissive.Sample(input.uv).rgb;

    float3 color = ambient + finalColor + emissive;
    color = color / (color + float3(1.0));
    color = pow(color, float3(1.0 / 2.2));
    return float4(color, 1.0);
}