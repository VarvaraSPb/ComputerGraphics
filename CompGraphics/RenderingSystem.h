#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <DirectXCollision.h>
#include <memory>
#include <numeric>
#include <random>
#include "d3dx12.h"
#include "OBJLoader.h"
#include "TextureLoader.h"
#include "InputDevice.h"
#include "Gbuffer.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct Vertex { XMFLOAT3 Position; XMFLOAT3 Normal; XMFLOAT2 TexCoord; };

struct alignas(256) ConstantBufferData {
    XMFLOAT4X4 World;
    XMFLOAT4X4 View;
    XMFLOAT4X4 Proj;
    XMFLOAT4X4 WorldInvTranspose;
    XMFLOAT4 MaterialDiffuse;
    XMFLOAT4 MaterialSpecular;
    int HasTexture;
    float TexTilingX;
    float TexTilingY;
    float TotalTime;
    float TexScrollX;
    float TexScrollY;
    XMFLOAT2 Pad1;
    XMFLOAT3 EyePosW;
    float DisplacementScale;
    float TessNearDist;
    float TessFarDist;
    XMFLOAT2 Pad2;
};

struct GpuMaterial {
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> textureUpload;
    ComPtr<ID3D12Resource> normalTexture;
    ComPtr<ID3D12Resource> normalUpload;
    ComPtr<ID3D12Resource> displacementTexture;
    ComPtr<ID3D12Resource> displacementUpload;
    int srvHeapIndex = -1;
    XMFLOAT4 diffuse = { 0.8f, 0.8f, 0.8f, 1.f };
    XMFLOAT4 specular = { 0.5f, 0.5f, 0.5f, 1.f };
    float shininess = 32.f;
    bool hasTexture = false;
};

struct PointLight {
    XMFLOAT4 Position;
    XMFLOAT4 Color;
};

struct SpotLight {
    XMFLOAT4 Position;
    XMFLOAT4 Direction;
    XMFLOAT4 Color;
};

struct alignas(256) LightBufferData {
    XMFLOAT4 DirLightDir;
    XMFLOAT4 DirLightColor;
    SpotLight SpotLights[2];
    int NumSpotLights;
    XMFLOAT3 Pad0;
    XMFLOAT4 AmbientColor;
    XMFLOAT4 EyePos;
};

struct RockInstance {
    XMMATRIX World;
    DirectX::BoundingBox Bounds;
};

struct OctreeNode {
    DirectX::BoundingBox Bounds;
    std::vector<size_t> Indices;
    std::array<std::unique_ptr<OctreeNode>, 8> Children;
    bool IsLeaf = true;
};

struct alignas(256) ShadowCBData {
    XMFLOAT4X4 LightViewProj[4];
    XMFLOAT4 CascadeSplits;
    XMFLOAT4 LightDir;
    XMFLOAT4 LightPos;
    XMFLOAT4 ShadowMapSize;
    float ShadowBias;
    float PCFRadius;
    float Padding[2];
};

struct CascadeData {
    XMMATRIX ViewProj;
    float SplitDistance;
    float NearPlane;
    float FarPlane;
};

class RenderingSystem
{
public:
    static constexpr UINT FRAME_COUNT = 2;
    static constexpr UINT MAX_TEXTURES = 128;
    static constexpr UINT MAX_SUBSETS = 512;
    static constexpr UINT MAX_RAIN_LIGHTS = 300;

    static constexpr UINT MAX_CASCADES = 4;
    static constexpr UINT SHADOW_MAP_SIZE = 4096;

    RenderingSystem() = default;
    ~RenderingSystem();

    bool Init(HWND hwnd, int width, int height);
    void BeginFrame(const float clearColor[4]);
    void DrawScene(float totalTime, float deltaTime);
    void EndFrame();
    void OnResize(int width, int height);
    bool LoadObj(const std::string& path);
    bool LoadStump(const std::string& path);
    void SetTexTiling(float x, float y) { m_texTiling = { x, y }; }
    void SetTexScroll(float x, float y) { m_texScroll = { x, y }; }
    void UpdateCamera(float deltaTime, const InputDevice& input);
    void SetDeferredRendering(bool enable) { m_useDeferredRendering = enable; }
    void GenerateRocks(int count, float spawnRadius);
    void SetMotionBlurIntensity(float intensity) { m_motionBlurIntensity = intensity; }
    void SetMotionBlurSamples(float samples) { m_motionBlurSamples = samples; }
    void SetBloomIntensity(float intensity) { m_bloomIntensity = intensity; }
    void SetPostEffectMode(int mode) { m_postEffectMode = mode; } 

private:
    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain(HWND hwnd, int width, int height);
    void CreateDescriptorHeaps();
    void CreateRenderTargetViews();
    void CreateDepthStencilView();
    void CreateFence();
    void CompileShaders();
    void CompileGeometryShaders();
    void CompileLightingShaders();
    void CreateRootSignature();
    void CreatePipelineStateObject();
    void CreateGeometryPassPSO();
    void CreateLightingRootSignature();
    void CreateLightingPassPSO();
    void CreateCubeGeometry();
    void UploadMeshToGpu(const std::vector<Vertex>& verts, const std::vector<UINT>& indices);
    void CreateScreenQuad();
    void CreateConstantBuffer();
    void LoadMaterials(const ObjMesh& mesh, const std::string& baseDir);
    void CreateLightingResources();
    void CreateRainLightBuffer();
    void CreateRainLightSRV();
    void CreateDefaultTextures();
    void RenderGeometryPass(float totalTime);
    void RenderLightingPass();
    void RenderForwardPass(float totalTime);
    void UpdateRainLights(float deltaTime);
    void UploadRainLightsToGPU();
    void AddLight();
    void WaitForGPU();
    void FlushCommandQueue();
    void MoveToNextFrame();
    float GetVerticalAngle() const;
    void LoadRock(const std::string& path);
    void BuildOctree();
    void BuildOctreeRecursive(OctreeNode& node, const std::vector<size_t>& indices, int depth, int maxDepth);
    void UpdateCulling(const XMMATRIX& viewProj);
    void CullOctreeRecursive(const OctreeNode* node, const DirectX::BoundingFrustum& frustum);
    void RenderRocks(float totalTime);

    void CreateShadowMapResources();
    void RenderShadowMap(const XMMATRIX& lightViewProj, int cascadeIndex);
    void RenderGeometryForShadowMap(const XMMATRIX& viewProj);
    void UpdateCascades(const XMMATRIX& view, const XMMATRIX& proj, const XMFLOAT3& lightDir);
    void UpdateShadowConstantBuffer();
    void CreateShadowMapPSO();
    void CompileShadowShaders();
    void CreateShadowMapRootSignature();

    // particles
    static constexpr UINT MAX_PARTICLES = 5000;
    static constexpr UINT PARTICLE_CB_OFFSET = 150 + (MAX_TEXTURES * 3) + (MAX_CASCADES * 2) + 16; // = 534 + 8 + 16 = 558

    static constexpr UINT POST_PROCESS_DESC_START = 650;

    struct Particle {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 velocity;
        float lifetime;
        float size;
        DirectX::XMFLOAT4 color;
        uint32_t isActive;
        uint32_t pad[3];
    };

    struct alignas(256) ParticleUpdateCB {
        DirectX::XMFLOAT3 gGravity;       float gDeltaTime;
        float gSpawnRate;                 float gMaxLifetime;
        float _pad0;                      float _pad1;
        DirectX::XMFLOAT3 gSpawnMin;      float _pad2;
        DirectX::XMFLOAT3 gSpawnMax;      float _pad3;
        float gTotalTime;                 float gWindStrength;
        float _pad4;                      float _pad5;
        DirectX::XMFLOAT3 gWindDirection; uint32_t gMaxParticles;
        uint32_t gSeed;                   float _pad6[3];
    };

    struct alignas(256) ParticleRenderCB {
        DirectX::XMFLOAT4X4 gView;
        DirectX::XMFLOAT4X4 gProj;
        DirectX::XMFLOAT3 gCameraPos;
        float _pad;
    };

    ComPtr<ID3D12Resource> m_particleBufferAppend;
    ComPtr<ID3D12Resource> m_particleBufferConsume;
    ComPtr<ID3D12Resource> m_particleUploadBuffer;
    ComPtr<ID3D12Resource> m_particleUpdateCB;
    ComPtr<ID3D12Resource> m_particleRenderCB;
    ParticleUpdateCB* m_particleUpdateCBData = nullptr;
    ParticleRenderCB* m_particleRenderCBData = nullptr;

    UINT m_particleDescStart = PARTICLE_CB_OFFSET;

    ComPtr<ID3D12RootSignature> m_particleComputeRootSig;
    ComPtr<ID3D12PipelineState> m_particleComputePSO;
    ComPtr<ID3DBlob> m_particleCSBlob;

    ComPtr<ID3D12RootSignature> m_particleRenderRootSig;
    ComPtr<ID3D12PipelineState> m_particleRenderPSO;
    ComPtr<ID3DBlob> m_particleVSBlob;
    ComPtr<ID3DBlob> m_particleGSBlob;
    ComPtr<ID3DBlob> m_particlePSBlob;

    bool m_isAppendActive = true;

    void CompileParticleShaders();
    void CreateParticleResources();
    void CreateParticleRootSignatures();
    void CreateParticlePSOs();
    void UpdateParticles(float deltaTime, float totalTime);
    void RenderParticles();

    void CreatePostProcessResources();
    void CompilePostProcessShaders();
    void CreatePostProcessRootSignature();
    void CreatePostProcessPSOs();
    void ApplyPostProcessing();

    ComPtr<ID3D12Resource> m_shadowMaps[MAX_CASCADES];
    ComPtr<ID3D12Resource> m_shadowCB;
    ShadowCBData* m_shadowCBData = nullptr;
    CascadeData m_cascades[MAX_CASCADES];
    float m_cascadeSplits[MAX_CASCADES];
    int m_numCascades = 3;
    XMFLOAT3 m_lightDir = { -0.5f, -1.0f, -0.3f };
    float m_shadowBias = 0.0015f;
    float m_pcfRadius = 2.0f;
    UINT m_shadowMapSRVStart = 0;
    UINT m_shadowMapDSVStart = 1;

    // shadow map PSO
    ComPtr<ID3D12PipelineState> m_shadowMapPSO;
    ComPtr<ID3D12RootSignature> m_shadowMapRootSig;
    ComPtr<ID3DBlob> m_shadowVSBlob;
    ComPtr<ID3DBlob> m_shadowPSBlob;

    ComPtr<ID3D12Resource> m_shadowMapCB;
    struct ShadowMapCBData {
        XMFLOAT4X4 WorldViewProj;
    };
    ShadowMapCBData* m_shadowMapCBData = nullptr;
    // end

    ComPtr<ID3D12Device> m_device;
    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12CommandQueue> m_cmdQueue;
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12CommandAllocator> m_cmdAllocators[FRAME_COUNT];
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Resource> m_renderTargets[FRAME_COUNT];
    UINT m_frameIndex = 0;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_cbvSrvHeap;
    UINT m_rtvDescSize = 0;
    UINT m_cbvSrvDescSize = 0;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValues[FRAME_COUNT]{};
    HANDLE m_fenceEvent = nullptr;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3DBlob> m_vsBlob;
    ComPtr<ID3DBlob> m_psBlob;
    ComPtr<ID3DBlob> m_hsBlob;
    ComPtr<ID3DBlob> m_dsBlob;
    ComPtr<ID3D12PipelineState> m_geometryPassPSO;
    ComPtr<ID3D12PipelineState> m_wireframePSO;
    ComPtr<ID3D12PipelineState> m_lightingPassPSO;
    ComPtr<ID3D12RootSignature> m_lightingRootSignature;
    ComPtr<ID3DBlob> m_lightingVSBlob;
    ComPtr<ID3DBlob> m_lightingPSBlob;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vbView{};
    D3D12_INDEX_BUFFER_VIEW m_ibView{};
    std::vector<MeshSubset> m_subsets;
    std::vector<GpuMaterial> m_gpuMaterials;

    ComPtr<ID3D12Resource> m_shadowVB;
    ComPtr<ID3D12Resource> m_shadowIB;
    D3D12_VERTEX_BUFFER_VIEW m_shadowVbView{};
    D3D12_INDEX_BUFFER_VIEW m_shadowIbView{};
    std::vector<MeshSubset> m_shadowSubsets;

    ComPtr<ID3D12Resource> m_stumpVertexBuffer;
    ComPtr<ID3D12Resource> m_stumpIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_stumpVbView{};
    D3D12_INDEX_BUFFER_VIEW m_stumpIbView{};
    std::vector<MeshSubset> m_stumpSubsets;
    std::vector<GpuMaterial> m_stumpMaterials;
    ComPtr<ID3D12Resource> m_defaultDiffuseTex;
    ComPtr<ID3D12Resource> m_defaultNormalTex;
    ComPtr<ID3D12Resource> m_defaultDisplacementTex;
    ComPtr<ID3D12Resource> m_defaultDiffuseUpload;
    ComPtr<ID3D12Resource> m_defaultNormalUpload;
    ComPtr<ID3D12Resource> m_defaultDisplacementUpload;
    UINT m_currentSrvSlot = 7;
    ComPtr<ID3D12Resource> m_constantBuffer;
    ConstantBufferData* m_cbMapped = nullptr;
    UINT m_cbSlotSize = 0;
    ComPtr<ID3D12Resource> m_lightBuffer;
    LightBufferData* m_lightMappedData = nullptr;
    ComPtr<ID3D12Resource> m_pointLightBuffer;
    PointLight* m_pointLightsMapped = nullptr;
    struct RainLight {
        PointLight data; bool active = false;
        XMFLOAT3 velocity{ 0.f, -200.f, 0.f };
        float lifeTime = 0.f;
    };
    std::vector<RainLight> m_rainLights;
    float m_spawnTimer = 0.f;
    float m_spawnInterval = 0.005f;
    XMFLOAT3 m_spawnAreaMin{ -800.f, 20.f, -350.f };
    XMFLOAT3 m_spawnAreaMax{ 750.f, 30.f, 300.f };
    float m_floorY = -1.5f;
    UINT m_activeLightCount = 0;
    Gbuffer m_gbuffer;
    ComPtr<ID3D12Resource> m_depthStencil;
    ComPtr<ID3D12Resource> m_screenQuadVB;
    D3D12_VERTEX_BUFFER_VIEW m_screenQuadVBView{};
    XMFLOAT2 m_texTiling = { 1.f, 1.f };
    XMFLOAT2 m_texScroll = { 0.05f, 0.f };
    int m_width = 0;
    int m_height = 0;
    XMFLOAT3 m_eye = { -80.f, 20.f, -20.f };
    XMFLOAT3 m_target = { 0.f, 10.f, 0.f };
    XMFLOAT3 m_up = { 0.f, 1.f, 0.f };
    float m_cameraSpeed = 500.0f;
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 0.0f;
    float m_totalTime = 0.0f;
    bool m_initialized = false;
    bool m_useDeferredRendering = true;
    bool m_wireframeMode = false;
    bool m_tKeyPressed = false;
    float m_tesselationNearDist = 200.0f;
    float m_tesselationFarDist = 1500.0f;
    std::vector<RockInstance> m_rocks;
    DirectX::BoundingBox m_rockBaseBounds;
    std::unique_ptr<OctreeNode> m_octree;
    std::vector<size_t> m_visibleRocks;
    int m_cullingMode = 0;
    bool m_cullKeyPressed = false;
    ComPtr<ID3D12Resource> m_rockVertexBuffer;
    ComPtr<ID3D12Resource> m_rockIndexBuffer;
    ComPtr<ID3D12Resource> m_particleCounterAppend;
    ComPtr<ID3D12Resource> m_particleCounterConsume;
    D3D12_VERTEX_BUFFER_VIEW m_rockVbView{};
    D3D12_INDEX_BUFFER_VIEW m_rockIbView{};
    std::vector<MeshSubset> m_rockSubsets;
    std::vector<GpuMaterial> m_rockMaterials;

    // Post-Processing resources
    ComPtr<ID3D12Resource> m_hdrRenderTarget;
    ComPtr<ID3D12Resource> m_luminanceTexture;
    ComPtr<ID3D12Resource> m_tempBlurTexture;

    ComPtr<ID3D12RootSignature> m_postProcessRootSig;
    ComPtr<ID3D12PipelineState> m_luminancePSO;
    ComPtr<ID3D12PipelineState> m_toneMapPSO;
    ComPtr<ID3D12PipelineState> m_blurHorizontalPSO;
    ComPtr<ID3D12PipelineState> m_blurVerticalPSO;

    ComPtr<ID3DBlob> m_postProcessVSBlob;
    ComPtr<ID3DBlob> m_luminancePSBlob;
    ComPtr<ID3DBlob> m_toneMapPSBlob;
    ComPtr<ID3DBlob> m_blurHPSBlob;
    ComPtr<ID3DBlob> m_blurVPSBlob;

    ComPtr<ID3D12Resource> m_postProcessCB;
    struct PostProcessConstants {
        float gExposure;
        float gAdaptationSpeed;
        float gMiddleGray;
        float gLumWhite;
        float gDeltaTime;

        float gMotionBlurIntensity;
        float gMotionBlurSamples;
    };
    PostProcessConstants* m_postProcessCBData = nullptr;

    bool m_enableToneMapping = true;
    bool m_enableBloom = true;
    float m_exposure = 1.0f;
    float m_adaptationSpeed = 0.3f;

    int m_postEffectMode = 0; 
    float m_motionBlurIntensity = 0.5f;
    float m_motionBlurSamples = 12.0f;
    float m_bloomIntensity = 0.6f;

    UINT m_hdrRTVIndex = FRAME_COUNT;
    UINT m_lumRTVIndex = FRAME_COUNT + 1;
    UINT m_blurRTVIndex = FRAME_COUNT + 2;

    ComPtr<ID3D12Resource> m_bloomTexture;
    ComPtr<ID3D12PipelineState> m_bloomExtractPSO;
    ComPtr<ID3D12PipelineState> m_bloomCombinePSO;
    ComPtr<ID3DBlob> m_bloomExtractBlob;
    ComPtr<ID3DBlob> m_bloomCombineBlob;

    UINT m_bloomRTVIndex = FRAME_COUNT + 3;
    UINT m_bloomSRVIndex = POST_PROCESS_DESC_START + 3;

    ComPtr<ID3D12PipelineState> m_motionBlurPSO;
    ComPtr<ID3D12PipelineState> m_combinedPostEffectsPSO;
    ComPtr<ID3DBlob> m_motionBlurBlob;
    ComPtr<ID3DBlob> m_combinedPostEffectsBlob;

    ComPtr<ID3D12Resource> m_motionBlurCB;
    struct MotionBlurConstants {
        XMFLOAT4X4 gPrevViewProj;
        XMFLOAT4X4 gCurrViewProj;
    };
    MotionBlurConstants* m_motionBlurCBData = nullptr;

    XMMATRIX m_prevViewProj;  

    bool m_bKeyPressed = false;
    bool m_mKeyPressed = false;
    bool m_nKeyPressed = false;
};