cbuffer LightBuffer : register(b0)
{
    // Point lights 
    float4 pointLightPos0;
    float4 pointLightColor0;
    float4 pointLightParams0; 
    
    float4 pointLightPos1;
    float4 pointLightColor1;
    float4 pointLightParams1; 
    
    float4 pointLightPos2; 
    float4 pointLightColor2;
    float4 pointLightParams2; 
    
    // Directional light 
    float4 dirLightDir; 
    float4 dirLightColor;
    float4 dirLightParams;
    
    // Spot light
    float4 spotLightPos; 
    float4 spotLightDir; 
    float4 spotLightColor;
    float4 spotLightParams; 
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
    
    float3 finalColor = albedo.xyz * 0.1f; 
    
    // 1. Point light 0 - красный
    if (pointLightParams0.z > 0.5f)
    {
        float3 toLight = pointLightPos0.xyz - worldPos.xyz;
        float dist = length(toLight);
        if (dist < pointLightParams0.y)
        {
            float3 lightDir = toLight / dist;
            float ndotl = max(0, dot(normal, lightDir));
            float atten = 1.0f - (dist / pointLightParams0.y);
            atten = atten * atten;
            finalColor += albedo.xyz * ndotl * pointLightColor0.xyz * pointLightParams0.x * atten;
        }
    }
    
    // 2. Point light 1 - зел
    if (pointLightParams1.z > 0.5f)
    {
        float3 toLight = pointLightPos1.xyz - worldPos.xyz;
        float dist = length(toLight);
        if (dist < pointLightParams1.y)
        {
            float3 lightDir = toLight / dist;
            float ndotl = max(0, dot(normal, lightDir));
            float atten = 1.0f - (dist / pointLightParams1.y);
            atten = atten * atten;
            finalColor += albedo.xyz * ndotl * pointLightColor1.xyz * pointLightParams1.x * atten;
        }
    }
    
    // 3. Point light 2 - синий
    if (pointLightParams2.z > 0.5f)
    {
        float3 toLight = pointLightPos2.xyz - worldPos.xyz;
        float dist = length(toLight);
        if (dist < pointLightParams2.y)
        {
            float3 lightDir = toLight / dist;
            float ndotl = max(0, dot(normal, lightDir));
            float atten = 1.0f - (dist / pointLightParams2.y);
            atten = atten * atten;
            finalColor += albedo.xyz * ndotl * pointLightColor2.xyz * pointLightParams2.x * atten;
        }
    }
    
    // 4. Directional light - фиол
    if (dirLightParams.y > 0.5f)
    {
        float3 lightDir = normalize(-dirLightDir.xyz);
        float ndotl = max(0, dot(normal, lightDir));
        finalColor += albedo.xyz * ndotl * dirLightColor.xyz * dirLightParams.x;
    }
    
    // 5. Spot light - жёлтый
    if (spotLightParams.w > 0.5f)
    {
        float3 toLight = spotLightPos.xyz - worldPos.xyz;
        float dist = length(toLight);
        if (dist < spotLightParams.y)
        {
            float3 lightDir = toLight / dist;
            float3 spotDir = normalize(-spotLightDir.xyz);
            
            float cosAngle = dot(lightDir, spotDir);
            float cosOuter = cos(radians(spotLightParams.z));
            float cosInner = cos(radians(spotLightParams.z * 0.7f));
            
            if (cosAngle > cosOuter)
            {
                float ndotl = max(0, dot(normal, lightDir));
                float atten = 1.0f - (dist / spotLightParams.y);
                atten = atten * atten;
                float spotFactor = saturate((cosAngle - cosOuter) / (cosInner - cosOuter));
                
                finalColor += albedo.xyz * ndotl * spotLightColor.xyz * spotLightParams.x * atten * spotFactor;
            }
        }
    }
    
    return float4(finalColor, albedo.a);
}