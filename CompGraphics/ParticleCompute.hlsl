#define FOUNTAIN_COUNT 5000 

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

cbuffer UpdateCB : register(b0)
{
    float3 gGravity;
    float gDeltaTime;
    float gSpawnRate;
    float gMaxLifetime;
    float3 gSpawnMin;
    float _pad0;
    float3 gSpawnMax;
    float _pad1;
    float gTotalTime;
    float gWindStrength;
    float3 gWindDirection;
    uint gMaxParticles;
    uint gSeed;
    float _pad2[3];
};

RWStructuredBuffer<Particle> g_Input : register(u0);
RWStructuredBuffer<Particle> g_Output : register(u1);

float Rand(uint s)
{
    return frac(sin(dot(float3(s, s * 113.0, s * 227.0), float3(12.9898, 78.233, 45.164))) * 43758.5453);
}

[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gMaxParticles)
        return;

    Particle p = g_Output[id.x];

    float dt = gDeltaTime;
    uint seed = gSeed + id.x * 7919u;

    if (p.isActive == 0)
    {
        if (id.x < FOUNTAIN_COUNT)
        {
            float gate = Rand(seed + uint(gTotalTime * 97.0));
            if (gate < 0.12)  
            {
                float3 t = float3(Rand(seed + 1), Rand(seed + 2), Rand(seed + 3));
                p.position = lerp(gSpawnMin, gSpawnMax, t);
                
                float ang = Rand(seed + 4) * 6.2831853;
                float rad = sqrt(Rand(seed + 5)) * 18.0;
                float up = 50.0 + Rand(seed + 6) * 20.0; 
                p.velocity = float3(cos(ang) * rad, up, sin(ang) * rad);
                
                p.lifetime = 2.5 + Rand(seed + 7) * (gMaxLifetime - 2.5); 
                p.size = 6.0 + Rand(seed + 8) * 4.0; 
                p.color = float4(0.3, 0.6, 1.0, 1.0);
                p.isActive = 1;
            }
        }
    }
    else
    {
        p.velocity += (gGravity + gWindDirection * gWindStrength) * dt;
        p.position += p.velocity * dt;
        p.lifetime -= dt;

        float lifeRatio = saturate(p.lifetime / gMaxLifetime);
        p.color = lerp(float4(0.05, 0.2, 0.9, 1.0), float4(0.7, 0.9, 1.0, 1.0), 1.0 - lifeRatio);

        if (p.lifetime <= 0.0)
            p.isActive = 0;
    }

    g_Output[id.x] = p;
}