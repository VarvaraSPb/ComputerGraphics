cbuffer LightBuffer : register(b0)
{
    float4 lightPos0; 
    float4 lightColor0;
    float4 lightParams0; 
    
    float4 lightPos1; 
    float4 lightColor1; 
    float4 lightParams1; 
    
    float4 lightPos2; 
    float4 lightColor2; 
    float4 lightParams2;
}

Texture2D albedoTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D positionTexture : register(t3);
SamplerState samplerState : register(s0);

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

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = input.position;
    output.texCoord = input.texCoord;
    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    float4 albedo = albedoTexture.Sample(samplerState, input.texCoord);
    float4 normalData = normalTexture.Sample(samplerState, input.texCoord);
    float4 worldPos = positionTexture.Sample(samplerState, input.texCoord);
    
    float3 normal = normalize(normalData.xyz * 2.0f - 1.0f);
    
    //basa
    float3 sunDir = float3(0, -1, 0);
    float sunNdotL = max(0, dot(normal, sunDir));
    float3 finalColor = albedo.xyz * (0.3f + sunNdotL * 0.7f);
    
    //1
    if (lightParams0.z > 0.5f)
    {
        float3 toLight = lightPos0.xyz - worldPos.xyz;
        float dist = length(toLight);
        if (dist < lightParams0.y)
        {
            float3 lightDir = toLight / dist;
            float pointNdotL = max(0, dot(normal, lightDir));
            float atten = 1.0f - (dist / lightParams0.y);
            atten = atten * atten;
            finalColor += albedo.xyz * pointNdotL * lightColor0.xyz * lightParams0.x * atten;
        }
    }
    
    //2
    if (lightParams1.z > 0.5f)
    {
        float3 toLight = lightPos1.xyz - worldPos.xyz;
        float dist = length(toLight);
        if (dist < lightParams1.y)
        {
            float3 lightDir = toLight / dist;
            float pointNdotL = max(0, dot(normal, lightDir));
            float atten = 1.0f - (dist / lightParams1.y);
            atten = atten * atten;
            finalColor += albedo.xyz * pointNdotL * lightColor1.xyz * lightParams1.x * atten;
        }
    }
    
    //3
    if (lightParams2.z > 0.5f)
    {
        float3 toLight = lightPos2.xyz - worldPos.xyz;
        float dist = length(toLight);
        if (dist < lightParams2.y)
        {
            float3 lightDir = toLight / dist;
            float pointNdotL = max(0, dot(normal, lightDir));
            float atten = 1.0f - (dist / lightParams2.y);
            atten = atten * atten;
            finalColor += albedo.xyz * pointNdotL * lightColor2.xyz * lightParams2.x * atten;
        }
    }
    
    return float4(finalColor, albedo.a);
}