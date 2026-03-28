#define MAX_POINT_LIGHTS 3
#define MAX_SPOT_LIGHTS  2

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
    
    PointLight gPointLights[MAX_POINT_LIGHTS];
    SpotLight gSpotLights[MAX_SPOT_LIGHTS];
    
    int gNumPointLights;
    int gNumSpotLights;
    
    float2 gPad;
    
    float4 gAmbientColor;
    float4 gEyePos;
};

Texture2D gAlbedoMap : register(t0); 
Texture2D gNormalMap : register(t1);
Texture2D gPositionMap : register(t2); 
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

float3 PhongBRDF(
    float3 N, 
    float3 V,
    float3 L,
    float3 albedo, 
    float specIntensity, 
    float specPow, 
    float3 lightColor, 
    float lightIntensity 
)
{
    float NdotL = max(dot(N, L), 0.0f);
    float3 diffuse = NdotL * albedo * lightColor;
    float3 R = reflect(-L, N);
    float RdotV = max(dot(R, V), 0.0f);
    float3 specular = pow(RdotV, max(specPow, 1.0f)) * specIntensity * lightColor;
    return (diffuse + specular) * lightIntensity;
}

float3 CalcDirectional(
    float3 N, float3 V, float3 pos,
    float3 albedo, float specIntensity, float specPow)
{
    if (gDirLightColor.w <= 0.0f)
        return float3(0, 0, 0);
    
    float3 L = normalize(-gDirLightDir.xyz);
    return PhongBRDF(N, V, L, albedo, specIntensity, specPow,
                     gDirLightColor.xyz, gDirLightColor.w);
}

float3 CalcPoint(
    PointLight light,
    float3 N, float3 V, float3 pos,
    float3 albedo, float specIntensity, float specPow)
{
    float3 toLight = light.Position.xyz - pos;
    float dist = length(toLight);
    float radius = light.Position.w;
    
    if (dist >= radius)
        return float3(0, 0, 0);
    
    float3 L = toLight / dist;
    
    float attenuation = 1.0f - smoothstep(0.0f, radius, dist);
    
    return PhongBRDF(N, V, L, albedo, specIntensity, specPow,
                     light.Color.xyz, light.Color.w) * attenuation;
}

float3 CalcSpot(
    SpotLight light,
    float3 N, float3 V, float3 pos,
    float3 albedo, float specIntensity, float specPow)
{
    float3 toLight = light.Position.xyz - pos;
    float dist = length(toLight);
    
    if (dist <= 0.001f)
        return float3(0, 0, 0);
    
    float3 L = toLight / dist;
    float3 spotDir = normalize(light.Direction.xyz);
    float cosAngle = dot(-L, spotDir);
    float cosInner = light.Position.w;
    float cosOuter = light.Direction.w;
    float spotFactor = smoothstep(cosOuter, cosInner, cosAngle);
    if (spotFactor <= 0.0f)
        return float3(0, 0, 0);
    
    float attenuation = 1.0f / (1.0f + 0.001f * dist * dist);
    return PhongBRDF(N, V, L, albedo, specIntensity, specPow, light.Color.xyz, light.Color.w) * attenuation * spotFactor;
}

float4 PSMain(PSInput input) : SV_Target
{
    float4 albedoData = gAlbedoMap.Sample(gSampler, input.texCoord);
    float4 normalData = gNormalMap.Sample(gSampler, input.texCoord);
    float4 positionData = gPositionMap.Sample(gSampler, input.texCoord);
    
    float3 albedo = albedoData.rgb;
    float specIntensity = albedoData.a; 
    float3 N = normalize(normalData.rgb * 2.0f - 1.0f); 
    float specPow = max(normalData.a, 1.0f); 
    float3 pos = positionData.rgb;
    
    if (length(pos) < 0.001f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    
    float3 V = normalize(gEyePos.xyz - pos);
    float3 lighting = gAmbientColor.rgb * albedo;
    lighting += CalcDirectional(N, V, pos, albedo, specIntensity, specPow);
    
    for (int i = 0; i < gNumPointLights; ++i)
    {
        lighting += CalcPoint(gPointLights[i], N, V, pos,
                              albedo, specIntensity, specPow);
    }
    
    for (int j = 0; j < gNumSpotLights; ++j)
    {
        lighting += CalcSpot(gSpotLights[j], N, V, pos,
                             albedo, specIntensity, specPow);
    }
    return float4(lighting, albedoData.a);
}