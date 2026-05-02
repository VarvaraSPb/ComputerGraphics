cbuffer GBufferCB : register(b0)
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
    float2 gPad1;
    
    float3 gEyePosW;
    float gDisplacementScale;
    
    float gTessNearDist;
    float gTessFarDist;
    float2 gPad2;
};

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);
SamplerState gSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct VSOutput
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct HS_CONSTANT_DATA_OUTPUT
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};

struct HS_CONTROL_POINT_OUTPUT
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexCoord : TEXCOORD;
};

//Vertex Shader 
VSOutput VSMain(VSInput vin)
{
    VSOutput vout;
    
    float4 posW = mul(float4(vin.Position, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = mul(vin.Normal, (float3x3) gWorldInvTranspose);
    
    float2 uv = vin.TexCoord;
    uv.x = uv.x * gTexTilingX + gTotalTime * gTexScrollX;
    uv.y = uv.y * gTexTilingY + gTotalTime * gTexScrollY;
    vout.TexCoord = uv;
    
    return vout;
}

//Hull Shader
HS_CONSTANT_DATA_OUTPUT CalcHSPatchConstants(
    InputPatch<VSOutput, 3> ip,
    uint PatchID : SV_PrimitiveID)
{
    HS_CONSTANT_DATA_OUTPUT Output;
    float3 centerW = (ip[0].PosW + ip[1].PosW + ip[2].PosW) / 3.0f;
    float dist = distance(centerW, gEyePosW);
    float minDist = gTessNearDist;
    float maxDist = gTessFarDist;
    
    float maxTess = 16.0f;
    float minTess = 2.0f;
    
    float tessFactor = minTess;
    if (dist < maxDist)
    {
        float tess = maxTess * saturate((maxDist - dist) / (maxDist - minDist));
        tessFactor = max(minTess, tess);
    }
    else
    {
        tessFactor = minTess;
    }
    
    Output.Edges[0] = tessFactor;
    Output.Edges[1] = tessFactor;
    Output.Edges[2] = tessFactor;
    Output.Inside = tessFactor;
    return Output;
}

[domain("tri")]
[partitioning("fractional_even")] 
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("CalcHSPatchConstants")]
HS_CONTROL_POINT_OUTPUT HSMain(
    InputPatch<VSOutput, 3> ip,
    uint i : SV_OutputControlPointID,
    uint PatchID : SV_PrimitiveID)
{
    HS_CONTROL_POINT_OUTPUT Output;
    Output.PosW = ip[i].PosW;
    Output.NormalW = ip[i].NormalW;
    Output.TexCoord = ip[i].TexCoord;
    return Output;
}

struct DSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
};

// Domain Shader 
[domain("tri")]
DSOutput DSMain(
    HS_CONSTANT_DATA_OUTPUT input,
    float3 domain : SV_DomainLocation,
    const OutputPatch<HS_CONTROL_POINT_OUTPUT, 3> patch)
{
    DSOutput vout;
    
    float3 posW = patch[0].PosW * domain.x + patch[1].PosW * domain.y + patch[2].PosW * domain.z;
    float3 normalW = patch[0].NormalW * domain.x + patch[1].NormalW * domain.y + patch[2].NormalW * domain.z;
    float2 texCoord = patch[0].TexCoord * domain.x + patch[1].TexCoord * domain.y + patch[2].TexCoord * domain.z;

    normalW = normalize(normalW);

    // SDVIG PO Displacement Map
    if (gDisplacementScale > 0.0f)
    {
        float h = gDisplacementMap.SampleLevel(gSampler, texCoord, 0).r;
        
        h = (h - 0.5f) * 2.0f;

        posW += (h * gDisplacementScale) * normalW;
    }

    vout.PosW = posW;
    float4 posV = mul(float4(posW, 1.0f), gView);
    vout.PosH = mul(posV, gProj);
    vout.NormalW = normalW;
    vout.TexCoord = texCoord;

    return vout;
}

struct PSOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Position : SV_Target2;
};

// Pixel Shader
PSOutput PSMain(DSOutput pin)
{
    PSOutput pout;
    
    float4 albedo = gHasTexture ? gDiffuseMap.Sample(gSampler, pin.TexCoord) : gMaterialDiffuse;
    
    float3 dp1 = ddx(pin.PosW);
    float3 dp2 = ddy(pin.PosW);
    float2 duv1 = ddx(pin.TexCoord);
    float2 duv2 = ddy(pin.TexCoord);
    
    float3 N = normalize(pin.NormalW);
    float3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
    float det = duv1.x * duv2.y - duv2.x * duv1.y;
    
    if (abs(det) < 1e-5f)
    {
        pout.Normal = float4(N, 1.0f);
    }
    else
    {
        T = normalize(T - N * dot(N, T));
        float3 B = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        
        float3 normalMapSample = gNormalMap.Sample(gSampler, pin.TexCoord).xyz;
        
        float3 mappedNormal = normalMapSample * 2.0f - 1.0f;
        
        N = normalize(mul(mappedNormal, TBN));
        pout.Normal = float4(N, 1.0f);
    }
    
    pout.Albedo = albedo;
    pout.Position = float4(pin.PosW, 1.0f);
    
    return pout;
}