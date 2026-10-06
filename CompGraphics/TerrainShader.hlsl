cbuffer TerrainCB : register(b0)
{
    float4x4 gWorld; 
    float4x4 gView;
    float4x4 gProj;
    float4x4 gWorldInvTranspose;

    float4 gMaterialDiffuse;
    float4 gMaterialSpecular;

    int gHasTexture;
    float gTexTilingX;
    float gTexTilingY;
    float gTotalTime;

    float gTexScrollX;
    float gTexScrollY;
    float gHeightScale;
    float gTileWorldSize;
    
    float gUVMinX;
    float gUVMinY;
    float gUVSize;
    float gCurtainDrop;
    
    float gTileCenterX;
    float gTileCenterZ;
    float gWorldOffsetY;
    float gPad0;
};

Texture2D gHeightMap : register(t0);
Texture2D gDiffuseMap : register(t1);
Texture2D gNormalMap : register(t2);
SamplerState gSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION; 
    float2 TexCoord : TEXCOORD; 
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION0;
    float2 UV : TEXCOORD0;
    float2 WorldUV : TEXCOORD1;
    float IsCurtain : TEXCOORD2;
};

VSOutput VSMain(VSInput vin)
{
    VSOutput vout;

    bool isCurtain = (vin.TexCoord.x < -0.5f);

    float2 localUV = float2(vin.Position.x, vin.Position.z);
    float2 clampedLocalUV = saturate(localUV);
    float2 worldUV = saturate(float2(gUVMinX, gUVMinY) + clampedLocalUV * gUVSize);

    float h = gHeightMap.SampleLevel(gSampler, worldUV, 0).r * gHeightScale;

    float3 posW;
    posW.x = gTileCenterX + (vin.Position.x - 0.5f) * gTileWorldSize;
    posW.z = gTileCenterZ + (vin.Position.z - 0.5f) * gTileWorldSize;
    posW.y = h + gWorldOffsetY;

    if (isCurtain)
    {
        posW.y = h + gWorldOffsetY + (vin.Position.y * gCurtainDrop);
    }
    
    float4 posV = mul(float4(posW, 1.0f), gView);
    vout.PosH = mul(posV, gProj);
    vout.PosW = posW;
    vout.UV = localUV;
    vout.WorldUV = worldUV;
    vout.IsCurtain = isCurtain ? 1.0f : 0.0f;

    return vout;
}

struct PSOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Position : SV_Target2;
    float4 MatRMA : SV_Target3;
};

PSOutput PSMain(VSOutput pin)
{
    PSOutput pout;
    
    float3 baseColor = gDiffuseMap.Sample(gSampler, pin.WorldUV).rgb;
    
    float2 detailUV = pin.PosW.xz * 0.05f;
    float3 detailColor = gDiffuseMap.Sample(gSampler, detailUV).rgb;
    
    float3 albedo = lerp(baseColor, baseColor * detailColor * 1.5f, 0.35f) * gMaterialDiffuse.rgb;
    
    float texelStep = 0.0005f;
    float hL = gHeightMap.SampleLevel(gSampler, pin.WorldUV + float2(-texelStep, 0.0f), 0).r * gHeightScale;
    float hR = gHeightMap.SampleLevel(gSampler, pin.WorldUV + float2(texelStep, 0.0f), 0).r * gHeightScale;
    float hD = gHeightMap.SampleLevel(gSampler, pin.WorldUV + float2(0.0f, -texelStep), 0).r * gHeightScale;
    float hU = gHeightMap.SampleLevel(gSampler, pin.WorldUV + float2(0.0f, texelStep), 0).r * gHeightScale;

    float worldStep = texelStep * 2000.0f * 2.0f;
    float3 N = normalize(float3(hL - hR, worldStep, hD - hU));

    pout.Albedo = float4(albedo, 1.0f);
    pout.Normal = float4(N * 0.5f + 0.5f, 1.0f);
    pout.Position = float4(pin.PosW, 1.0f);
    pout.MatRMA = float4(0.85f, 0.0f, 1.0f, 1.0f);

    return pout;
}