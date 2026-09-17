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
    float4 gCameraForward;
};

#define NUM_CASCADES 4
#define PCF_RADIUS 1
#define PCF_SAMPLES ((PCF_RADIUS * 2 + 1) * (PCF_RADIUS * 2 + 1))

cbuffer ShadowCB : register(b1)
{
    float4x4 gLightViewProj[NUM_CASCADES];
    float4 gCascadeSplits;
    float4 gLightDir;
    float4 gLightPos;
    float4 gShadowMapSize;
    float gShadowBias;
    float gPCFRadius;
    float gShadowTextureTiling; 
    float gShadowTextureStrength; 
    float4 gShadowEyePos; 
};

Texture2D gShadowMap0 : register(t4);
Texture2D gShadowMap1 : register(t5);
Texture2D gShadowMap2 : register(t6);
Texture2D gShadowMap3 : register(t7);
Texture2D gCatTexture : register(t8);
SamplerComparisonState gShadowSampler : register(s1);

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
    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texelSize;
            float2 sampleUV = uv + offset;
            
            if (sampleUV.x < 0.0f || sampleUV.x > 1.0f ||
                sampleUV.y < 0.0f || sampleUV.y > 1.0f)
            {
                visibility += 1.0f;
                continue;
            }
            
            float sampledDepth = shadowMap.SampleLevel(gSampler, sampleUV, 0.0f).r;
            visibility += ((compareDepth) <= sampledDepth) ? 1.0f : 0.0f;
        }
    }
    return visibility / 9.0f;
}

float CalculateShadow(float3 worldPos, float3 normal)
{
    float3 lightDir = normalize(-gLightDir.xyz);
    float NdotL = saturate(dot(normal, lightDir));
    float depthBias = gShadowBias;

    [unroll]
    for (int c = 0; c < NUM_CASCADES; ++c)
    {
        float texelWorldSize =
            (c == 0) ? gCascadeSplits.x :
            (c == 1) ? gCascadeSplits.y :
            (c == 2) ? gCascadeSplits.z : gCascadeSplits.w;

        float offsetAmount = texelWorldSize * (1.0f + (1.0f - NdotL) * 3.0f);
        float3 offsetPos = worldPos + normal * offsetAmount;

        float4 lightClip = mul(float4(offsetPos, 1.0f), gLightViewProj[c]);
        float3 ndc = lightClip.xyz / max(lightClip.w, 0.0001f);
        float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f.xx;

        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
            continue;
        if (ndc.z < 0.0f || ndc.z > 1.0f)
            continue;

        float compareDepth = ndc.z - depthBias;
        float2 texelSize = 1.0f / gShadowMapSize.xy;
        
        int radius = (int) gPCFRadius;
        if (radius < 1)
            radius = 1;
        if (radius > 4)
            radius = 4;

        float visibility = 0.0f;
        float samples = 0.0f;

        [loop]
        for (int y = -radius; y <= radius; ++y)
        {
            [loop]
            for (int x = -radius; x <= radius; ++x)
            {
                if (x * x + y * y > radius * radius + 1)
                    continue;

                float2 offset = float2(x, y) * texelSize;
                float2 sampleUV = uv + offset;

                if (sampleUV.x < 0.0f || sampleUV.x > 1.0f ||
                    sampleUV.y < 0.0f || sampleUV.y > 1.0f)
                {
                    visibility += 1.0f;
                    samples += 1.0f;
                    continue;
                }

                float sampledDepth = 0.0f;
                if (c == 0)
                    sampledDepth = gShadowMap0.SampleLevel(gSampler, sampleUV, 0).r;
                else if (c == 1)
                    sampledDepth = gShadowMap1.SampleLevel(gSampler, sampleUV, 0).r;
                else if (c == 2)
                    sampledDepth = gShadowMap2.SampleLevel(gSampler, sampleUV, 0).r;
                else
                    sampledDepth = gShadowMap3.SampleLevel(gSampler, sampleUV, 0).r;

                visibility += (compareDepth <= sampledDepth) ? 1.0f : 0.0f;
                samples += 1.0f;
            }
        }

        return visibility / max(samples, 1.0f);
    }

    return 1.0f;
}

float4 PSMain(PSInput input) : SV_Target
{
    float4 albedoData = gAlbedoMap.Sample(gSampler, input.texCoord);
    float4 normalData = gNormalMap.Sample(gSampler, input.texCoord);
    float4 positionData = gPositionMap.Sample(gSampler, input.texCoord);

    float3 albedo = albedoData.rgb;
    float3 pos = positionData.rgb;

    if (positionData.a < 0.5f)
        return float4(0.05, 0.05, 0.08, 1.0);
    
    float3 N = normalData.rgb * 2.0 - 1.0;
    float nLen = length(N);
    if (nLen < 0.01)
        N = float3(0.0, 1.0, 0.0);
    else
        N /= nLen;

    float3 V = normalize(gEyePos.xyz - pos);
    
    float shadowFactor = CalculateShadow(pos, N);
    
    if (gShadowTextureStrength > 0.001f)
    {
        float inShadow = 1.0f - shadowFactor;
        if (inShadow > 0.95f)
        {
            float3 n = abs(N);
            
            float scale = gShadowTextureTiling * 0.0008f;
            float3 worldUV = pos * scale;
            
            float3 colorX = gCatTexture.Sample(gSampler, frac(worldUV.zy)).rgb;
            float3 colorY = gCatTexture.Sample(gSampler, frac(worldUV.xz)).rgb;
            float3 colorZ = gCatTexture.Sample(gSampler, frac(worldUV.xy)).rgb;

            float3 blend = n / (n.x + n.y + n.z + 0.001f);
            float3 catColor = colorX * blend.x + colorY * blend.y + colorZ * blend.z;

            catColor *= 2.0f;

            float intensity = saturate((inShadow - 0.95f) / 0.05f) * gShadowTextureStrength;
            intensity = min(intensity, 0.75f);
            albedo = lerp(albedo, catColor, intensity);
        }
    }
    
    float3 finalColor = albedo * gAmbientColor.xyz * gAmbientColor.w;
   
    float3 L = normalize(-gLightDir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    finalColor += NdotL * albedo * gDirLightColor.xyz * gDirLightColor.w
                  * shadowFactor;

    // red
    float3 redLightPos = float3(-200.0, 80.0, -150.0);
    float3 toLightRed = redLightPos - pos;
    float distRed = length(toLightRed);
    if (distRed < 250.0 && distRed > 0.01)
    {
        float3 lDirRed = toLightRed / distRed;
        float attRed = pow(1.0 - (distRed / 250.0), 2.0);
        finalColor += max(dot(N, lDirRed), 0.0) * float3(1.0, 0.2, 0.2)
                      * 1.0 * attRed * shadowFactor;
    }

    // green
    float3 greenLightPos = float3(200.0, 70.0, 150.0);
    float3 toLightGreen = greenLightPos - pos;
    float distGreen = length(toLightGreen);
    if (distGreen < 250.0 && distGreen > 0.01)
    {
        float3 lDirGreen = toLightGreen / distGreen;
        float attGreen = pow(1.0 - (distGreen / 250.0), 2.0);
        finalColor += max(dot(N, lDirGreen), 0.0) * float3(0.2, 1.0, 0.2)
                      * 1.0 * attGreen * shadowFactor;
    }

    // blue
    float3 blueLightPos = float3(-100.0, 500.0, -200.0);
    float3 toLightBlue = blueLightPos - pos;
    float distBlue = length(toLightBlue);
    if (distBlue < 250.0 && distBlue > 0.01)
    {
        float3 lDirBlue = toLightBlue / distBlue;
        float attBlue = pow(1.0 - (distBlue / 250.0), 2.0);
        finalColor += max(dot(N, lDirBlue), 0.0) * float3(0.2, 0.2, 1.0)
                      * 1.0 * attBlue * shadowFactor;
    }

    // orange
    float3 orangeLightPos = float3(250.0, 530.0, 280.0);
    float3 toLightOrange = orangeLightPos - pos;
    float distOrange = length(toLightOrange);
    if (distOrange < 300.0 && distOrange > 0.01)
    {
        float3 lDirOrange = toLightOrange / distOrange;
        float attOrange = pow(1.0 - (distOrange / 300.0), 2.0);
        finalColor += max(dot(N, lDirOrange), 0.0) * float3(1.0, 0.5, 0.1)
                      * 1.5 * attOrange * shadowFactor;
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
            finalColor += light.Color.rgb * light.Color.w
                          * intensity * 1.0 * shadowFactor;
        }

        if (distToLight < rainRadius * 2.0)
        {
            float glowIntensity = 1.0 - (distToLight / (rainRadius * 2.0));
            finalColor += light.Color.rgb * light.Color.w
                          * glowIntensity * 0.3 * shadowFactor;
        }
    }

    return float4(finalColor, 1.0);
}