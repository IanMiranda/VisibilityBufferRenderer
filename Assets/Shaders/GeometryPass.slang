#extension GL_EXT_nonuniform_qualifier : require

import Common;

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct VSOutput
{
    float4 svPosition : SV_Position;
    float3 posView : POSITION0;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 posLight : POSITION1;
    float3x3 tbn : TBN;
};

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output = (VSOutput)0;

    float4x4 mv = mul(gPassData.view, gObjectData.model);
    float4x4 mvp = mul(gPassData.viewProj, gObjectData.model);
    float4x4 mvpLight = mul(gPassData.viewProjLight, gObjectData.model);

    float3 T = normalize(mul(mv, float4(input.tangent, 0.0))).xyz;
    float3 B = normalize(mul(mv, float4(input.bitangent, 0.0))).xyz;
    float3 N = normalize(mul(mv, float4(input.normal, 0.0))).xyz;
    output.tbn = transpose(float3x3(T, B, N));

    output.svPosition = mul(mvp, float4(input.position, 1.0));
    output.posView = mul(mv, float4(input.position, 1.0)).xyz;
    output.posLight = mul(mvpLight, float4(input.position, 1.0));
    output.posLight.y *= -1.0f;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

struct PSOutput
{
    float4 position : SV_Target0;
    float4 normal : SV_Target1;
    float4 albedo : SV_Target2;
    float4 specular : SV_Target3;
};

[shader("fragment")]
PSOutput FSMain(VSOutput input)
{
    float3 N = gTextures[gObjectData.normalMapIndex].Sample(input.uv).rgb;
    N = N * 2.0 - 1.0;
    N = normalize(mul(input.tbn, N));

    PSOutput output = (PSOutput)0;
    output.position = float4(input.posView, 1.0);
    output.normal = float4(N, 1.0);
    output.albedo = gTextures[gObjectData.diffuseMapIndex].Sample(input.uv);
    output.specular = gTextures[gObjectData.specularMapIndex].Sample(input.uv);
    return output;
}