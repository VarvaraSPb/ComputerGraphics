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

SamplerState gPointSampler : register(s0);
SamplerState gLinearSampler : register(s1);

Texture2D<float4> gHDRColor : register(t0);
Texture2D<float> gLuminance : register(t1);
Texture2D<float4> gBloomTexture : register(t2);
Texture2D<float> gDepthTexture : register(t3); 

cbuffer ToneMappingCB : register(b0)
{
    float gExposure;
    float gAdaptationSpeed;
    float gMiddleGray;
    float gLumWhite;
    float gDeltaTime;
    
    float gMotionBlurIntensity; 
    float gMotionBlurSamples; 
    float2 gPad;
};

static const float3 LUMINANCE_FACTOR = float3(0.2126, 0.7152, 0.0722);

float4 LuminancePS(PSInput input) : SV_Target0
{
    float3 hdrColor = gHDRColor.Sample(gPointSampler, input.texCoord).rgb;
    float luminance = dot(hdrColor, LUMINANCE_FACTOR);
    luminance = clamp(luminance, 0.0f, 10.0f);
    return float4(luminance, luminance, luminance, 1.0f);
}

float4 BloomExtractPS(PSInput input) : SV_Target0
{
    float3 hdrColor = gHDRColor.Sample(gLinearSampler, input.texCoord).rgb;
    float luminance = dot(hdrColor, LUMINANCE_FACTOR);
    float threshold = 1.0f;
    float3 bloomColor = max(hdrColor - threshold, 0.0f);
    return float4(bloomColor, 1.0f);
}

float4 BloomCombinePS(PSInput input) : SV_Target0
{
    float3 hdrColor = gHDRColor.Sample(gLinearSampler, input.texCoord).rgb;
    float3 bloomColor = gBloomTexture.Sample(gLinearSampler, input.texCoord).rgb;
    float bloomIntensity = 0.6f;
    float3 finalColor = hdrColor + bloomColor * bloomIntensity;
    
    finalColor *= gExposure;
    finalColor = finalColor / (finalColor + 1.0f);
    finalColor = pow(max(finalColor, 0.0f), 1.0f / 2.2f);
    return float4(finalColor, 1.0f);
}

cbuffer BlurCB : register(b0)
{
    float2 gTexelSize;
};

static const float gBlurWeights[7] =
{
    0.0909f, 0.0876f, 0.0793f, 0.0673f,
    0.0535f, 0.0399f, 0.0279f
};

float4 BlurHorizontalPS(PSInput input) : SV_Target0
{
    float4 color = gHDRColor.SampleLevel(gLinearSampler, input.texCoord, 0) * gBlurWeights[0];
    for (int i = 1; i < 7; i++)
    {
        float2 offset = float2(gTexelSize.x * i, 0.0f);
        color += gHDRColor.SampleLevel(gLinearSampler, input.texCoord + offset, 0) * gBlurWeights[i];
        color += gHDRColor.SampleLevel(gLinearSampler, input.texCoord - offset, 0) * gBlurWeights[i];
    }
    return color;
}

float4 BlurVerticalPS(PSInput input) : SV_Target0
{
    float4 color = gHDRColor.SampleLevel(gLinearSampler, input.texCoord, 0) * gBlurWeights[0];
    for (int i = 1; i < 7; i++)
    {
        float2 offset = float2(0.0f, gTexelSize.y * i);
        color += gHDRColor.SampleLevel(gLinearSampler, input.texCoord + offset, 0) * gBlurWeights[i];
        color += gHDRColor.SampleLevel(gLinearSampler, input.texCoord - offset, 0) * gBlurWeights[i];
    }
    return color;
}

float4 ToneMapPS(PSInput input) : SV_Target0
{
    float3 hdrColor = gHDRColor.Sample(gLinearSampler, input.texCoord).rgb;
    hdrColor = min(hdrColor, 20.0f);
    hdrColor *= gExposure;
    float3 mappedColor = hdrColor / (hdrColor + 1.0f);
    mappedColor = pow(max(mappedColor, 0.0f), 1.0f / 2.2f);
    return float4(mappedColor, 1.0f);
}

cbuffer MotionBlurCB : register(b1)
{
    float4x4 gPrevViewProj; 
    float4x4 gCurrViewProj;
};

float4 MotionBlurPS(PSInput input) : SV_Target0
{
    float2 uv = input.texCoord;
    
    float3 color = gHDRColor.Sample(gLinearSampler, uv).rgb;
    float depth = gDepthTexture.Sample(gPointSampler, uv).r;
    
    float samples = max(4.0f, gMotionBlurSamples);
    float intensity = gMotionBlurIntensity * 0.5f;
    
    float2 center = float2(0.5f, 0.5f);
    float2 dir = uv - center;
    float dist = length(dir);
    
    float blurAmount = intensity * dist * 2.0f;
    
    float3 blurredColor = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    
    for (int i = 0; i < samples; i++)
    {
        float t = (float) i / (samples - 1.0f) - 0.5f;
        float2 sampleUV = uv + dir * t * blurAmount;
        
        float3 sampleColor = gHDRColor.Sample(gLinearSampler, sampleUV).rgb;
        
        float weight = exp(-t * t * 4.0f);
        
        blurredColor += sampleColor * weight;
        totalWeight += weight;
    }
    
    blurredColor /= totalWeight;
    
    float3 finalColor = lerp(color, blurredColor, gMotionBlurIntensity * 0.8f);
    
    finalColor *= gExposure;
    finalColor = finalColor / (finalColor + 1.0f);
    finalColor = pow(max(finalColor, 0.0f), 1.0f / 2.2f);
    
    return float4(finalColor, 1.0f);
}

float4 CombinedPostEffectsPS(PSInput input) : SV_Target0
{
    float2 uv = input.texCoord;
    float3 hdrColor = gHDRColor.Sample(gLinearSampler, uv).rgb;
    
    float luminance = dot(hdrColor, LUMINANCE_FACTOR);
    float3 bloomColor = max(hdrColor - 1.0f, 0.0f);
    bloomColor = gBloomTexture.Sample(gLinearSampler, uv).rgb * 0.6f;
    
    float2 center = float2(0.5f, 0.5f);
    float2 dir = uv - center;
    float dist = length(dir);
    float blurAmount = gMotionBlurIntensity * dist * 2.0f;
    
    float3 blurredColor = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    float samples = max(4.0f, gMotionBlurSamples);
    
    for (int i = 0; i < samples; i++)
    {
        float t = (float) i / (samples - 1.0f) - 0.5f;
        float2 sampleUV = uv + dir * t * blurAmount;
        float3 sampleColor = gHDRColor.Sample(gLinearSampler, sampleUV).rgb;
        float weight = exp(-t * t * 4.0f);
        blurredColor += sampleColor * weight;
        totalWeight += weight;
    }
    blurredColor /= totalWeight;
    
    float3 finalColor = lerp(hdrColor, blurredColor, gMotionBlurIntensity * 0.8f);
    finalColor += bloomColor * 0.6f;
    
    finalColor *= gExposure;
    finalColor = finalColor / (finalColor + 1.0f);
    finalColor = pow(max(finalColor, 0.0f), 1.0f / 2.2f);
    
    return float4(finalColor, 1.0f);
}