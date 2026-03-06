cbuffer ConstantBuffer : register(b0)
{
    matrix world;
    matrix view;
    matrix proj;
    matrix worldInvTranspose;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    float4 eyePos;
    float4 materialDiffuse;
    float4 materialSpecular;
    float specularPower;
    float totalTime;
    float texTilingX;
    float texTilingY;
    float texScrollX;
    float texScrollY;
    int hasTexture;
    float3 padding;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
};

Texture2D diffuseTexture : register(t0);
SamplerState samplerState : register(s0);

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    output.worldPos = worldPos.xyz;
    output.position = mul(worldPos, view);
    output.position = mul(output.position, proj);
    
    output.normal = mul(input.normal, (float3x3) worldInvTranspose);
    output.normal = normalize(output.normal);
    
    float2 uv = input.texCoord * float2(texTilingX, texTilingY);
    uv.x += totalTime * texScrollX;
    uv.y += totalTime * texScrollY;
    output.texCoord = uv;
    
    return output;
}

struct PSOutput
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 specular : SV_Target2;
    float4 position : SV_Target3;
};

PSOutput PSMain(VSOutput input)
{
    PSOutput output;
    
    float4 texColor = diffuseTexture.Sample(samplerState, input.texCoord);
    output.albedo = materialDiffuse * texColor;
    
    output.normal = float4(input.normal * 0.5f + 0.5f, 1.0f);
    output.specular = materialSpecular;
    output.position = float4(input.worldPos, 1.0f);
    
    return output;
}