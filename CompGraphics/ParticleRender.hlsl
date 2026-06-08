cbuffer RenderCB : register(b0)
{
    float4x4 gView;
    float4x4 gProj;
    float3 gCamPos;
    float _pad;
};

struct Particle
{
    float3 position;
    float3 velocity;
    float lifetime;
    float size;
    float4 color;
    uint isActive;
    uint pad[3];
};

StructuredBuffer<Particle> gParticles : register(t0);

struct VSOut
{
    float4 worldPos : TEXCOORD0;
    float size : TEXCOORD1;
    float4 color : COLOR0;
    uint active : TEXCOORD2;
};

VSOut VSMain(uint vertexID : SV_VertexID)
{
    VSOut o = (VSOut) 0;
    Particle p = gParticles[vertexID];
    o.worldPos = float4(p.position, 1.0);
    o.size = p.size;
    o.color = p.color;
    o.active = p.isActive;
    return o;
}

struct GSOut
{
    float4 posH : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0; 
};

[maxvertexcount(4)]
void GSMain(point VSOut input[1], inout TriangleStream<GSOut> stream)
{
    if (input[0].active == 0)
        return;

    float hs = input[0].size * 0.5;
    
    float4 viewCenter = mul(float4(input[0].worldPos.xyz, 1.0), gView);

    float2 off[4] =
    {
        float2(-1, -1),
        float2(+1, -1),
        float2(-1, +1),
        float2(+1, +1)
    };

    for (int i = 0; i < 4; ++i)
    {
        float3 corner = viewCenter.xyz + float3(off[i] * hs, 0.0);
        GSOut o;
        o.posH = mul(float4(corner, 1.0), gProj);
        o.color = input[0].color;
        o.uv = off[i];
        stream.Append(o);
    }
    stream.RestartStrip();
}

float4 PSMain(GSOut i) : SV_Target
{
    float r2 = dot(i.uv, i.uv);
    clip(1.0 - r2);
    
    float shade = sqrt(saturate(1.0 - r2));
    float3 rgb = i.color.rgb * (0.35 + 0.65 * shade);
    return float4(rgb, 1.0);
}
