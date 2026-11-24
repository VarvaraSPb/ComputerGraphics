cbuffer TerrainCB : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4x4 gWorldInvTranspose;
    
    float4x4 gPrevWorld;
    float4x4 gPrevViewProj;
    
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

    float2 gJitter;
    float2 gPad1;

    float4 gBrush; 
};

Texture2D gHeightMap : register(t0);
Texture2D gDiffuseMap : register(t1);
Texture2D gNormalMap : register(t2);
Texture2D gPaintMask : register(t3);
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
    float4 CurClipPos : TEXCOORD3;
    float4 PrevClipPos : TEXCOORD4;
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
    float4 clip = mul(posV, gProj);
    
    vout.CurClipPos = clip;
    vout.PrevClipPos = mul(float4(posW, 1.0f), gPrevViewProj);
    
    vout.PosH = clip;
    vout.PosH.xy += gJitter * clip.w;

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
    float2 Velocity : SV_Target3;
};

PSOutput PSMain(VSOutput pin)
{
    PSOutput pout;
    
    float3 albedo = gDiffuseMap.Sample(gSampler, pin.WorldUV).rgb * gMaterialDiffuse.rgb;
    
    float mask = gPaintMask.Sample(gSampler, pin.WorldUV).r;
    float3 uvColor = float3(pin.UV.x, pin.UV.y, 0.0f);
    albedo = lerp(albedo, uvColor, mask);
    
    if (gBrush.w > 0.5f)
    {
        float d = length(pin.PosW.xz - gBrush.xy);
        float ringWidth = max(gBrush.z * 0.04f, 0.5f);
        float ring = 1.0f - smoothstep(0.0f, ringWidth, abs(d - gBrush.z));
        albedo = lerp(albedo, float3(1.0f, 1.0f, 1.0f), ring);
    }
    
    float3 dp1 = ddx(pin.PosW);
    float3 dp2 = ddy(pin.PosW);
    float3 crossVal = cross(dp2, dp1);
    float len = length(crossVal);
    float3 N = (len > 1e-5f) ? (crossVal / len) : float3(0.0f, 1.0f, 0.0f);
    if (N.y < 0.0f)
        N = -N;
    
    float3 texNormal = gNormalMap.Sample(gSampler, pin.WorldUV).rgb * 2.0f - 1.0f;
    N = normalize(N + float3(texNormal.x, 0.0f, texNormal.y) * 0.5f);

    pout.Albedo = float4(albedo, 1.0f);
    pout.Normal = float4(N * 0.5f + 0.5f, 0.85f);
    pout.Position = float4(pin.PosW, 1.0f);

    float2 curUV = (pin.CurClipPos.xy / pin.CurClipPos.w) * float2(0.5f, -0.5f) + 0.5f;
    float2 prevUV = (pin.PrevClipPos.xy / pin.PrevClipPos.w) * float2(0.5f, -0.5f) + 0.5f;
    pout.Velocity = curUV - prevUV;

    return pout;
}