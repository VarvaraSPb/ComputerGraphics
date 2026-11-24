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
#include "Terrain.h"

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
    XMFLOAT4X4 PrevWorld;    
    XMFLOAT4X4 PrevViewProj; 
    XMFLOAT4 MaterialDiffuse;
    XMFLOAT4 MaterialSpecular;
    int HasTexture;
    float TexTilingX;
    float TexTilingY;
    float TotalTime;
    float TexScrollX;
    float TexScrollY;
    XMFLOAT2 Jitter; 
    XMFLOAT3 EyePosW;
    float DisplacementScale;
    float TessNearDist;
    float TessFarDist;
    float EnableWave;
    XMFLOAT2 Pad2;
};

struct GpuMaterial {
    ComPtr<ID3D12Resource> texture;         
    ComPtr<ID3D12Resource> textureUpload;
    ComPtr<ID3D12Resource> normalTexture;
    ComPtr<ID3D12Resource> normalUpload;
    ComPtr<ID3D12Resource> metallicTexture; 
    ComPtr<ID3D12Resource> metallicUpload;
    ComPtr<ID3D12Resource> roughnessTexture; 
    ComPtr<ID3D12Resource> roughnessUpload;
    int srvHeapIndex = -1;                 
    XMFLOAT4 diffuse = { 0.8f, 0.8f, 0.8f, 1.f };
    float metallic = 0.0f;
    float roughness = 0.5f;
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
    UINT NumPointLights;         
    XMFLOAT2 Pad0;               
    XMFLOAT4 AmbientColor;
    XMFLOAT4 EyePos;
    XMFLOAT4 CameraForward;
    float PrefilterMipCount;     
    float Pad1[3];
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
    float ShadowTextureTiling;   
    float ShadowTextureStrength;   
    XMFLOAT4 EyePos;              
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
    static constexpr UINT MAX_SUBSETS = 1024;
    static constexpr UINT MAX_RAIN_LIGHTS = 64;

    static constexpr UINT MAX_CASCADES = 4;
    static constexpr UINT SHADOW_MAP_SIZE = 2048;

    RenderingSystem() = default;
    ~RenderingSystem();

    bool Init(HWND hwnd, int width, int height);
    void BeginFrame(const float clearColor[4]);
    void DrawScene(float totalTime, float deltaTime);
    void EndFrame();
    void OnResize(int width, int height);
    bool LoadObj(const std::string& path);
    bool LoadStump(const std::string& path);
    bool LoadCerberus(const std::string& path);
    void SetTexTiling(float x, float y) { m_texTiling = { x, y }; }
    void SetTexScroll(float x, float y) { m_texScroll = { x, y }; }
    void UpdateCamera(float deltaTime, const InputDevice& input);
    void SetDeferredRendering(bool enable) { m_useDeferredRendering = enable; }
    void GenerateRocks(int count, float spawnRadius);
    void SetMotionBlurIntensity(float intensity) { m_motionBlurIntensity = intensity; }
    void SetMotionBlurSamples(float samples) { m_motionBlurSamples = samples; }
    void SetBloomIntensity(float intensity) { m_bloomIntensity = intensity; }
    void SetPostEffectMode(int mode) { m_postEffectMode = mode; }

    bool LoadCatPattern(const std::wstring& path);
    void SetShadowTextureTiling(float t) { m_shadowTextureTiling = t; }
    void SetShadowTextureStrength(float s) { m_shadowTextureStrength = s; }

    void BeginEvent(const wchar_t* name)
    {
        m_cmdList->BeginEvent(1, name, (UINT)((wcslen(name) + 1) * sizeof(wchar_t)));
    }
    void EndEvent() { m_cmdList->EndEvent(); }
    void NameResources();

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
    void InitStaticCascades();
    void CreateShadowMapPSO();
    void CompileShadowShaders();
    void CreateShadowMapRootSignature();
    void CompileTerrainShaders();
    void CreateTerrainRootSignature();
    void CreateTerrainPSO();
    void RenderTerrainPass(const XMMATRIX& view, const XMMATRIX& proj);

    // particles
    static constexpr UINT MAX_PARTICLES = 5000;
    static constexpr UINT PARTICLE_CB_OFFSET = 150 + (MAX_TEXTURES * 3) + (MAX_CASCADES * 2) + 16; // = 534 + 8 + 16 = 558

    static constexpr UINT POST_PROCESS_DESC_START = 650;
    static constexpr UINT TAA_UAV_SLOT = POST_PROCESS_DESC_START + 16; 
    static constexpr UINT TAA_SRV_SLOT = POST_PROCESS_DESC_START + 20; 

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
    float m_cascadeSplits[MAX_CASCADES] = { 150.0f, 400.0f, 1000.0f, 2500.0f };
    int m_numCascades = 4;
    XMFLOAT3 m_lightDir = { 0.0f, -1.0f, 0.0f };
    float m_shadowBias = 0.0028f;
    float m_pcfRadius = 1.0f;
    UINT m_shadowMapSRVStart = 4;
    UINT m_shadowMapDSVStart = 1;

    struct CascadeWorldBounds {
        XMFLOAT3 Center;
        XMFLOAT3 Extents;
        float    SplitDistance;
    };
    CascadeWorldBounds m_cascadeBounds[MAX_CASCADES];
    bool m_cascadesInitialized = false;
    bool m_shadowMapsDirty = true;

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

	// cat shadow texture
    ComPtr<ID3D12Resource> m_catTexture;
    ComPtr<ID3D12Resource> m_catTextureUpload;
    float m_shadowTextureTiling = 8.0f;
    float m_shadowTextureStrength = 1.0f;
    bool  m_enableCatShadow = true;
    bool  m_cKeyPressed = false;
    static constexpr UINT CAT_TEXTURE_SLOT = 8; 

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
    Terrain m_terrain;

    ComPtr<ID3D12Resource> m_iblIrradiance;       
    ComPtr<ID3D12Resource> m_iblIrradianceUpload;
    ComPtr<ID3D12Resource> m_iblPrefilter;        
    ComPtr<ID3D12Resource> m_iblPrefilterUpload;
    ComPtr<ID3D12Resource> m_iblBRDFLUT;       
    ComPtr<ID3D12Resource> m_iblBRDFLUTUpload;
    bool m_iblLoaded = false;

    static constexpr UINT IBL_IRRADIANCE_SLOT = 9;
    static constexpr UINT IBL_PREFILTER_SLOT = 10;
    static constexpr UINT IBL_BRDFLUT_SLOT = 11;
    static constexpr UINT DEFAULT_BLACK_SLOT = 19;
    static constexpr UINT DEFAULT_WHITE_SLOT = 20;

    void LoadIBLTextures();

    ComPtr<ID3D12PipelineState> m_terrainPSO;
    ComPtr<ID3D12RootSignature> m_terrainRootSig;
    ComPtr<ID3DBlob> m_terrainVSBlob;
    ComPtr<ID3DBlob> m_terrainPSBlob;
    static constexpr UINT TERRAIN_SRV_SLOT = 14;
    static constexpr UINT TERRAIN_SRV_SLOT_DIFFUSE = 13;
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
    ComPtr<ID3D12Resource> m_cerberusVertexBuffer;
    ComPtr<ID3D12Resource> m_cerberusIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_cerberusVbView{};
    D3D12_INDEX_BUFFER_VIEW m_cerberusIbView{};
    std::vector<MeshSubset> m_cerberusSubsets;
    std::vector<GpuMaterial> m_cerberusMaterials;

    ComPtr<ID3D12Resource> m_defaultDiffuseTex;
    ComPtr<ID3D12Resource> m_defaultNormalTex;
    ComPtr<ID3D12Resource> m_defaultDisplacementTex;
    ComPtr<ID3D12Resource> m_defaultBlackTex;   
    ComPtr<ID3D12Resource> m_defaultWhiteTex;       
    ComPtr<ID3D12Resource> m_defaultDiffuseUpload;
    ComPtr<ID3D12Resource> m_defaultNormalUpload;
    ComPtr<ID3D12Resource> m_defaultDisplacementUpload;
    ComPtr<ID3D12Resource> m_defaultBlackUpload;
    ComPtr<ID3D12Resource> m_defaultWhiteUpload;
    UINT m_currentSrvSlot = 100;  
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
    float m_spawnInterval = 0.05f;
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
    XMFLOAT3 m_sceneCenter = { 0.0f, 500.0f, 0.0f };
    XMFLOAT3 m_sceneExtents = { 2000.0f, 1000.0f, 2000.0f };
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
    bool m_enableTAA = true;
    XMFLOAT2 m_currentJitter = { 0.0f, 0.0f };
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

    // TAA resources 
    ComPtr<ID3D12Resource> m_taaHistory[2];
    ComPtr<ID3D12RootSignature> m_taaRootSig;
    ComPtr<ID3D12PipelineState> m_taaPSO;
    ComPtr<ID3DBlob> m_taaCSBlob;

    ComPtr<ID3D12Resource> m_taaCB;
    struct TAAConstants {
        XMFLOAT4X4 gInvViewProj;
        XMFLOAT4X4 gPrevViewProj;
        XMFLOAT2   gResolution;
        float      gDebugMode;
        float      gHistoryValid;
        XMFLOAT2  gCurrentJitter;
        XMFLOAT2  gPrevJitter;
        XMFLOAT2  gPad;
    };
    TAAConstants* m_taaCBData = nullptr;

    UINT m_taaHistorySRVStart[2] = { 0, 0 };
    UINT m_taaHistoryUAVStart = 0;
    int m_taaCurrHistoryIndex = 0;
    bool m_taaHistoryValid = false;
    XMFLOAT2 m_prevJitter = { 0.0f, 0.0f };

    void CreateTAAResources();
    void CompileTAAShader();
    void CreateTAARootSignature();
    void CreateTAAPSO(); 
    void ApplyTAA();

    void HandleTAAKeys(const InputDevice& input);

    bool m_taaDebugVelocity = false;  
    bool m_taaHistoryHandling = true; 
    bool m_cerberusPaused = false;   
    bool m_yKeyPressed = false;
    bool m_hKeyPressed = false;
    bool m_rKeyPressed = false;
    bool m_pKeyPressed = false;

    float m_cerberusRotationAngle = 0.0f;
    XMMATRIX m_cerberusPrevWorld = XMMatrixIdentity();

	// desaturation effect parameters
    float m_mousePosX = 0.5f;
    float m_mousePosY = 0.5f;
    float m_desatRadius = 0.15f;
    float m_desatStrength = 1.0f;

    ComPtr<ID3D12Resource> m_postProcessCB;
    struct PostProcessConstants {
        float gExposure;
        float gAdaptationSpeed;
        float gMiddleGray;
        float gLumWhite;
        float gDeltaTime;
        float gMotionBlurIntensity;
        float gMotionBlurSamples;
        float gMousePosX;
        float gMousePosY;
        float gDesatRadius;
        float gDesatStrength;
        float gPad1;
        float gPad2;
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

    XMMATRIX m_prevViewProj = XMMatrixIdentity();

    bool m_bKeyPressed = false;
    bool m_mKeyPressed = false;
    bool m_nKeyPressed = false;

    static constexpr UINT BRUSH_MASK_SLOT = 700; 

    void UpdateBrush(const InputDevice& input);
    bool  m_brushEnabled = true;
    float m_brushRadius = 40.0f;

    ComPtr<ID3D12Resource> m_boxVB;
    ComPtr<ID3D12Resource> m_boxIB;
    D3D12_VERTEX_BUFFER_VIEW m_boxVbView{};
    D3D12_INDEX_BUFFER_VIEW m_boxIbView{};
    static constexpr UINT BOX_SRV_SLOT = 720;  
    float m_boxAngle = 0.0f;
    XMMATRIX m_boxPrevWorld = XMMatrixIdentity();
    bool m_boxPrevValid = false;
    XMFLOAT3 m_boxPos = { -30.0f, 12.0f, 40.0f };
    void CreateBoxMesh();
    void RenderBox(float totalTime);

    // Atmosphere
    struct alignas(256) AtmosphereCBData {
        XMFLOAT3 CameraPos;    float MetersPerUnit;
        XMFLOAT3 CamRight;     float TanHalfFovX;
        XMFLOAT3 CamUp;        float TanHalfFovY;
        XMFLOAT3 CamForward;   float SunIntensity;
        XMFLOAT3 SunDir;       float MieG;            
        XMFLOAT3 BetaRayleigh; float ObjectsEnabled;
        XMFLOAT3 BetaMie;      float MieExtinctionScale;
        float RayleighScaleHeight; float MieScaleHeight;
        float JitterX; float JitterY;
    };

    void CompileAtmosphereShaders();
    void CreateAtmospherePipeline();
    void HandleAtmosphereKeys(float deltaTime, const InputDevice& input);
    void UpdateAtmosphere(float deltaTime);
    void RenderAtmospherePass();

    ComPtr<ID3D12RootSignature> m_atmoRootSig;
    ComPtr<ID3D12PipelineState> m_atmoPSO;
    ComPtr<ID3DBlob> m_atmoVSBlob;
    ComPtr<ID3DBlob> m_atmoPSBlob;
    ComPtr<ID3D12Resource> m_atmoCB;
    UINT8* m_atmoCBMapped = nullptr;
    static constexpr UINT ATMO_GBUFFER_POSITION_SLOT = 2;   

    bool  m_atmoOnObjects = true;    
    float m_rayleighStrength = 1.0f;     
    float m_mieStrength = 1.0f;        
    float m_mieG = 0.76f;               
    float m_sunIntensity = 40.0f;       
    float m_atmoMetersPerUnit = 10.0f; 
    float m_sunAzimuth = 0.6f;           
    float m_sunElevation = 0.7f;        
    int   m_atmoPreset = 0;             
    bool  m_atmoBlending = false;    
    float m_atmoTargetRayleigh = 1.0f;
    float m_atmoTargetMie = 1.0f;
    float m_atmoTargetG = 0.76f;

    // DXR RT Pipeline
    struct RTShadowCBData {         
        XMFLOAT3 CameraPos; float NormalBias;
        XMFLOAT3 ToLight;   float LightType;    
        XMFLOAT3 LightPos;  float LightSize;     
        UINT Mode; UINT Rays; UINT Frame; UINT Pad0;  
        float TanHalfFovY; float ScreenH; float TMaxDir; float Pad1;
    };
    struct RTLightCBData {           
        XMFLOAT4 LightPosType;     
        XMFLOAT4 RadianceMode;      
        XMFLOAT4 Params;         
    };
    struct RTFilterCBData {     
        XMFLOAT2 Res; float Step; float Pad0;
        XMFLOAT3 CameraPos; float DepthSigma;
        float NormalPower; float Pad1[3];
    };
    struct RTBlas {
        ComPtr<ID3D12Resource> result, scratch;
        D3D12_GPU_VIRTUAL_ADDRESS addr = 0;
    };

    static constexpr UINT RT_DESC_START = 800;  

    void InitRaytracing();
    void CreateRTResources();
    bool RTLoadShaderBytes();
    void CreateRTPipeline();
    void CreateRTFilterPipeline();
    void RTBuildScene();
    void RTBuildBlas(const std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geoms, RTBlas& out);
    void RTBuildTerrainMesh(std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geoms);
    void RTBuildTlas();
    void RenderRTShadows();
    void HandleRTKeys(float dt, const InputDevice& input);
    void GetRTLightCB(RTLightCBData& out) const;
    ComPtr<ID3D12Resource> RTCreateBuffer(UINT64 size, D3D12_HEAP_TYPE type,
        D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state);

    ComPtr<ID3D12Device5> m_rtDevice;
    ComPtr<ID3D12GraphicsCommandList4> m_rtCmdList;
    bool m_rtSupported = false;
    bool m_rtSceneBuilt = false;
    int  m_rtScratchCountdown = 0;

    ComPtr<ID3D12RootSignature> m_rtGlobalRootSig;
    ComPtr<ID3D12StateObject> m_rtStateObject;
    ComPtr<ID3D12StateObjectProperties> m_rtStateProps;
    ComPtr<ID3D12Resource> m_rtShaderTable;
    std::vector<BYTE> m_rtShaderBytes;

    ComPtr<ID3D12RootSignature> m_rtFilterRootSig;
    ComPtr<ID3D12PipelineState> m_rtFilterPSO;
    ComPtr<ID3D12Resource> m_rtRaw, m_rtFilterA, m_rtFilterB;  
    UINT m_rtVisSlot = RT_DESC_START;                

    RTBlas m_rtBlasSponza, m_rtBlasStump, m_rtBlasCerberus, m_rtBlasRock, m_rtBlasBox, m_rtBlasTerrain;
    ComPtr<ID3D12Resource> m_rtTerrainVB, m_rtTerrainIB;
    ComPtr<ID3D12Resource> m_rtTlas[FRAME_COUNT], m_rtTlasScratch[FRAME_COUNT], m_rtTlasInstances[FRAME_COUNT];
    D3D12_RAYTRACING_INSTANCE_DESC* m_rtTlasInstancesMapped[FRAME_COUNT] = {};
    UINT m_rtTlasCapacity = 0;

    XMMATRIX m_rtStumpWorld = XMMatrixIdentity(); 
    XMMATRIX m_rtCerbWorld = XMMatrixIdentity();
    XMMATRIX m_rtBoxWorld = XMMatrixIdentity();

    int   m_rtMode = 3;         
    bool  m_rtDebugMask = false;    
    bool  m_rtFilterOn = true;     
    int   m_rtLightType = 0;        
    UINT  m_rtRays = 1;               
    float m_rtSunRadiusDeg = 1.5f;     
    float m_rtPointRadius = 25.0f;     
    XMFLOAT3 m_rtPointPos = { 150.0f, 300.0f, 60.0f };   
    float m_rtPointIntensity = 8.0f;
    UINT  m_rtFrame = 0;
    bool  m_rtKeyLatch[8] = {};
};