#define MAX_SPOT_LIGHTS 2

struct PointLight
{
    float4 Position;
    float4 Color;
};
struct SpotLight
{
    float4 Position;
    float4 Direction;
    float4 Color;
};

cbuffer LightingCB : register(b0)
{
    float4 gDirLightDir;
    float4 gDirLightColor;
    SpotLight gSpotLights[MAX_SPOT_LIGHTS];
    int gNumSpotLights;
    float3 gPad0;
    float4 gAmbientColor;
    float4 gEyePos;
};

Texture2D gAlbedoMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gPositionMap : register(t2);
StructuredBuffer<PointLight> gPointLights : register(t3);
SamplerState gSampler : register(s0);

struct VSInput
{
    float4 position : POSITION;
    float2 texCoord : TEXCOORD;
};
struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

PSInput VSMain(uint vertexID : SV_VertexID)
{
    PSInput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.texCoord = uv;
    output.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    float4 albedoData = gAlbedoMap.Sample(gSampler, input.texCoord);
    float4 normalData = gNormalMap.Sample(gSampler, input.texCoord);
    float4 positionData = gPositionMap.Sample(gSampler, input.texCoord);
    
    float3 albedo = albedoData.rgb;
    float3 pos = positionData.rgb;
    
    if (length(pos) < 0.1)
        return float4(0.05, 0.05, 0.08, 1.0);
    
    float3 N = normalData.rgb * 2.0 - 1.0;
    float nLen = length(N);
    if (nLen < 0.01)
        N = float3(0.0, 1.0, 0.0);
    else
        N /= nLen;
    
    float3 V = normalize(gEyePos.xyz - pos);
    
    float3 finalColor = albedo * gAmbientColor.xyz * gAmbientColor.w;
    
    float3 L = normalize(-gDirLightDir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    finalColor += NdotL * albedo * gDirLightColor.xyz * gDirLightColor.w;
    
    // red
    float3 redLightPos = float3(-200.0, 80.0, -150.0);
    float3 toLightRed = redLightPos - pos;
    float distRed = length(toLightRed);
    if (distRed < 250.0 && distRed > 0.01)
    {
        float3 lDirRed = toLightRed / distRed;
        float attRed = pow(1.0 - (distRed / 250.0), 2.0);
        finalColor += max(dot(N, lDirRed), 0.0) * float3(1.0, 0.2, 0.2) * 3.0 * attRed;
    }
    
    // green
    float3 greenLightPos = float3(200.0, 70.0, 150.0);
    float3 toLightGreen = greenLightPos - pos;
    float distGreen = length(toLightGreen);
    if (distGreen < 250.0 && distGreen > 0.01)
    {
        float3 lDirGreen = toLightGreen / distGreen;
        float attGreen = pow(1.0 - (distGreen / 250.0), 2.0);
        finalColor += max(dot(N, lDirGreen), 0.0) * float3(0.2, 1.0, 0.2) * 3.0 * attGreen;
    }
    
    // blue
    float3 blueLightPos = float3(-100.0, 500.0, -200.0);
    float3 toLightBlue = blueLightPos - pos;
    float distBlue = length(toLightBlue);
    if (distBlue < 250.0 && distBlue > 0.01)
    {
        float3 lDirBlue = toLightBlue / distBlue;
        float attBlue = pow(1.0 - (distBlue / 250.0), 2.0);
        finalColor += max(dot(N, lDirBlue), 0.0) * float3(0.2, 0.2, 1.0) * 3.0 * attBlue;
    }
    
    // orange
    float3 orangeLightPos = float3(250.0, 530.0, 280.0);
    float3 toLightOrange = orangeLightPos - pos;
    float distOrange = length(toLightOrange);
    if (distOrange < 300.0 && distOrange > 0.01)
    {
        float3 lDirOrange = toLightOrange / distOrange;
        float attOrange = pow(1.0 - (distOrange / 300.0), 2.0);
        finalColor += max(dot(N, lDirOrange), 0.0) * float3(1.0, 0.5, 0.1) * 4.5 * attOrange;
    }
    
    for (uint i = 0; i < 300; i++)
    {
        PointLight light = gPointLights[i];
        if (light.Position.w <= 0.5)
            continue;
    
        float3 toLightCenter = light.Position.xyz - pos;
        float distToLight = length(toLightCenter);
        float rainRadius = 3.0; 
        
        if (distToLight < rainRadius)
        {
            float intensity = 1.0 - (distToLight / rainRadius);
            finalColor += light.Color.rgb * light.Color.w * intensity * 3.0;
        }
        
        if (distToLight < rainRadius * 2.0)
        {
            float glowIntensity = 1.0 - (distToLight / (rainRadius * 2.0));
            finalColor += light.Color.rgb * light.Color.w * glowIntensity * 0.5;
        }
    }
    
    finalColor = pow(finalColor, 1.0 / 2.2);
    finalColor = max(finalColor, albedo * 0.1); // soften shadows
    
    return float4(finalColor, 1.0);
}