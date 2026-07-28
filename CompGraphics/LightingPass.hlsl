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

#define NUM_CASCADES 4
#define PCF_RADIUS 2
#define PCF_SAMPLES ((PCF_RADIUS * 2 + 1) * (PCF_RADIUS * 2 + 1))

Texture2D gShadowMap0 : register(t4);
Texture2D gShadowMap1 : register(t5);
Texture2D gShadowMap2 : register(t6);
Texture2D gShadowMap3 : register(t7);
SamplerComparisonState gShadowSampler : register(s1);

cbuffer ShadowCB : register(b1)
{
    float4x4 gLightViewProj[NUM_CASCADES];
    float4 gCascadeSplits;
    float4 gLightDir;
    float4 gLightPos;
    float4 gShadowMapSize;
    float gShadowBias;
    float gPCFRadius;
    float gPadding[2];
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

float PCF(Texture2D shadowMap, float2 uv, float compareDepth, float2 texelSize)
{
    float shadow = 0.0f;
    [unroll(5)]
    for (int y = -PCF_RADIUS; y <= PCF_RADIUS; y++)
    {
        [unroll(5)]
        for (int x = -PCF_RADIUS; x <= PCF_RADIUS; x++)
        {
            float2 offset = float2(x, y) * texelSize;
            float2 sampleUV = uv + offset;
            
            if (sampleUV.x < 0.0f || sampleUV.x > 1.0f ||
                sampleUV.y < 0.0f || sampleUV.y > 1.0f)
            {
                shadow += 1.0f; 
                continue;
            }
            
            float sampledDepth = shadowMap.SampleCmpLevelZero(gShadowSampler, sampleUV, compareDepth).r;
            shadow += sampledDepth;
        }
    }
    
    return shadow / (float) PCF_SAMPLES;
}

int SelectCascade(float depthInViewSpace)
{
    float absDepth = abs(depthInViewSpace);
    
    [unroll(NUM_CASCADES)]
    for (int i = 0; i < NUM_CASCADES - 1; i++)
    {
        if (absDepth < gCascadeSplits[i])
        {
            return i;
        }
    }
    return NUM_CASCADES - 1;
}

float CalculateShadowForCascade(float3 worldPos, float3 normal, int cascadeIndex)
{
    float4 lightSpacePos = mul(float4(worldPos, 1.0f), gLightViewProj[cascadeIndex]);
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    float2 shadowUV = projCoords.xy * 0.5f + 0.5f;
    
    float cascadeBias = gShadowBias * (1.0f + cascadeIndex * 0.3f);
    float compareDepth = projCoords.z - cascadeBias;
    
    if (shadowUV.x < 0.0f || shadowUV.x > 1.0f ||
        shadowUV.y < 0.0f || shadowUV.y > 1.0f)
    {
        return 0.0f;
    }
    
    float2 texelSize = 1.0f / gShadowMapSize.xy;
    
    float shadow = 0.0f;
    if (cascadeIndex == 0)
        shadow = PCF(gShadowMap0, shadowUV, compareDepth, texelSize);
    else if (cascadeIndex == 1)
        shadow = PCF(gShadowMap1, shadowUV, compareDepth, texelSize);
    else if (cascadeIndex == 2)
        shadow = PCF(gShadowMap2, shadowUV, compareDepth, texelSize);
    else if (cascadeIndex == 3)
        shadow = PCF(gShadowMap3, shadowUV, compareDepth, texelSize);
    
    return shadow;
}

float CalculateShadow(float3 worldPos, float3 normal)
{
    float depthInView = length(worldPos - gEyePos.xyz);
    float absDepth = abs(depthInView);
    
    int cascadeIndex = SelectCascade(depthInView);
    
    float blendFactor = 0.0f;
    if (cascadeIndex < NUM_CASCADES - 1)
    {
        float farSplit = gCascadeSplits[cascadeIndex];
        float blendRange = farSplit * 0.2f;
        float blendStart = farSplit - blendRange;
        
        if (absDepth > blendStart && absDepth < farSplit)
        {
            blendFactor = (absDepth - blendStart) / blendRange;
        }
    }
    
    float shadow1 = CalculateShadowForCascade(worldPos, normal, cascadeIndex);
    
    float shadow = shadow1;
    if (blendFactor > 0.0f && cascadeIndex < NUM_CASCADES - 1)
    {
        int nextCascade = cascadeIndex + 1;
        float shadow2 = CalculateShadowForCascade(worldPos, normal, nextCascade);
        shadow = lerp(shadow1, shadow2, blendFactor);
    }
    
    float NdotL = max(0.001f, dot(normal, -gLightDir.xyz));
    
    return shadow;
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
    
    float shadowFactor = CalculateShadow(pos, N);
    
    shadowFactor = pow(shadowFactor, 2.0f);
    
    float3 finalColor = albedo * gAmbientColor.xyz * gAmbientColor.w;
    
    float3 L = normalize(-gDirLightDir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    finalColor += NdotL * albedo * gDirLightColor.xyz * gDirLightColor.w * shadowFactor * 0.5;
    
    // red 
    float3 redLightPos = float3(-200.0, 80.0, -150.0);
    float3 toLightRed = redLightPos - pos;
    float distRed = length(toLightRed);
    if (distRed < 250.0 && distRed > 0.01)
    {
        float3 lDirRed = toLightRed / distRed;
        float attRed = pow(1.0 - (distRed / 250.0), 2.0);
        finalColor += max(dot(N, lDirRed), 0.0) * float3(1.0, 0.2, 0.2) * 1.0 * attRed * shadowFactor;
    }
    
    // green 
    float3 greenLightPos = float3(200.0, 70.0, 150.0);
    float3 toLightGreen = greenLightPos - pos;
    float distGreen = length(toLightGreen);
    if (distGreen < 250.0 && distGreen > 0.01)
    {
        float3 lDirGreen = toLightGreen / distGreen;
        float attGreen = pow(1.0 - (distGreen / 250.0), 2.0);
        finalColor += max(dot(N, lDirGreen), 0.0) * float3(0.2, 1.0, 0.2) * 1.0 * attGreen * shadowFactor;
    }
    
    // blue
    float3 blueLightPos = float3(-100.0, 500.0, -200.0);
    float3 toLightBlue = blueLightPos - pos;
    float distBlue = length(toLightBlue);
    if (distBlue < 250.0 && distBlue > 0.01)
    {
        float3 lDirBlue = toLightBlue / distBlue;
        float attBlue = pow(1.0 - (distBlue / 250.0), 2.0);
        finalColor += max(dot(N, lDirBlue), 0.0) * float3(0.2, 0.2, 1.0) * 1.0 * attBlue * shadowFactor;
    }
    
    // orange 
    float3 orangeLightPos = float3(250.0, 530.0, 280.0);
    float3 toLightOrange = orangeLightPos - pos;
    float distOrange = length(toLightOrange);
    if (distOrange < 300.0 && distOrange > 0.01)
    {
        float3 lDirOrange = toLightOrange / distOrange;
        float attOrange = pow(1.0 - (distOrange / 300.0), 2.0);
        finalColor += max(dot(N, lDirOrange), 0.0) * float3(1.0, 0.5, 0.1) * 1.5 * attOrange * shadowFactor;
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
            finalColor += light.Color.rgb * light.Color.w * intensity * 1.0 * shadowFactor;
        }
        
        if (distToLight < rainRadius * 2.0)
        {
            float glowIntensity = 1.0 - (distToLight / (rainRadius * 2.0));
            finalColor += light.Color.rgb * light.Color.w * glowIntensity * 0.3 * shadowFactor;
        }
    }
    
    finalColor = pow(finalColor, 1.0 / 2.2);
    finalColor = max(finalColor, albedo * 0.1); // soften shadows
    
    return float4(finalColor, 1.0);
}