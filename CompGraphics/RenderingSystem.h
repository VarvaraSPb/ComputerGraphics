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

class RenderingSystem
{
public:
    static constexpr UINT FRAME_COUNT = 2;
    static constexpr UINT MAX_TEXTURES = 128;
    static constexpr UINT MAX_SUBSETS = 512;
    static constexpr UINT MAX_RAIN_LIGHTS = 300;

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
    struct RainLight { PointLight data; bool active = false; XMFLOAT3 velocity{ 0.f, -200.f, 0.f }; float lifeTime = 0.f; };
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
};