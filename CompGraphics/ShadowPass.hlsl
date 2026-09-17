struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float Depth : TEXCOORD0;
};

cbuffer ShadowCB : register(b0)
{
    float4x4 WorldViewProj;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 posH = mul(float4(input.Position, 1.0f), WorldViewProj);
    output.Position = posH;
    output.Depth = posH.z / posH.w;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return float4(input.Depth, input.Depth, input.Depth, 1.0f);
}
