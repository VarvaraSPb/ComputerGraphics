#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <string>
#include <array>
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct TerrainVertex
{
    XMFLOAT3 Position;
    XMFLOAT2 TexCoord;
};

class Terrain
{
public:
    bool Init(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        ID3D12DescriptorHeap* srvHeap,
        UINT srvStartSlot,
        const std::wstring& heightMapPath,
        const std::wstring& diffuseMapPath,
        float worldSize = 2000.0f,
        float heightScale = 200.0f);

    void Update(const XMMATRIX& view,
        const XMMATRIX& proj,
        const XMFLOAT3& cameraPos,
        UINT screenWidth,
        UINT screenHeight);

    void Render(ID3D12GraphicsCommandList* cmdList,
        ID3D12RootSignature* rootSig,
        ID3D12PipelineState* pso,
        ID3D12Resource* cbBuffer,
        void* cbMapped,
        const XMMATRIX& view,
        const XMMATRIX& proj,
        UINT cbSlotSize,
        UINT frameIndex,
        float totalTime);

    ID3D12Resource* GetHeightMap() const { return m_heightMap.Get(); }
    ID3D12Resource* GetDiffuseMap() const { return m_diffuseMap.Get(); }
    ID3D12Resource* GetNormalMap() const { return m_normalMap.Get(); }
    UINT GetSRVStartSlot() const { return m_srvStartSlot; }

    struct Stats
    {
        int totalNodes = 0;
        int visibleTiles = 0;
        int maxDepth = 0;
        int drawnTilesThisFrame = 0;
    };
    Stats GetStats() const { return m_stats; }

    float m_sseThreshold = 8.0f;
    int   m_maxDepth = 6;
    int   m_baseMeshRes = 4;

private:
    struct QuadNode
    {
        DirectX::BoundingBox Bounds;
        XMFLOAT3 Center;
        float    Size;
        int      Depth;
        int      LOD;
        int      MeshLevel;
        bool     IsLeaf = true;
        bool     IsSplit = false;

        std::unique_ptr<QuadNode> Children[4];
    };

    void BuildQuadTree(QuadNode& node, int depth, int maxDepth);

    bool ShouldSplit(const QuadNode& node,
        const XMMATRIX& viewProj,
        const XMFLOAT3& cameraPos) const;

    void TraverseAndCollect(QuadNode& node,
        const DirectX::BoundingFrustum& frustum,
        const XMMATRIX& viewProj,
        const XMFLOAT3& cameraPos);

    void CreateMeshPyramid(ID3D12Device* device);

    void CreateSingleMesh(ID3D12Device* device,
        int res,
        bool addCurtains,
        ComPtr<ID3D12Resource>& outVB,
        ComPtr<ID3D12Resource>& outIB,
        D3D12_VERTEX_BUFFER_VIEW& outVBView,
        D3D12_INDEX_BUFFER_VIEW& outIBView,
        UINT& outIndexCount);

    bool LoadHeightMap(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::wstring& path);

    bool LoadDiffuseMap(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::wstring& path);

    float SampleHeightCPU(float u, float v) const;

    void GetNodeHeightRange(const QuadNode& node,
        float& outMin, float& outMax) const;

private:
    ComPtr<ID3D12Resource> m_heightMap;
    ComPtr<ID3D12Resource> m_heightMapUpload;
    ComPtr<ID3D12Resource> m_diffuseMap;
    ComPtr<ID3D12Resource> m_diffuseMapUpload;
    ComPtr<ID3D12Resource> m_normalMap;

    std::vector<float> m_heightData;
    UINT m_heightMapWidth = 0;
    UINT m_heightMapHeight = 0;

    static constexpr int MESH_LEVELS = 5;
    std::array<ComPtr<ID3D12Resource>, MESH_LEVELS> m_meshVBs;
    std::array<ComPtr<ID3D12Resource>, MESH_LEVELS> m_meshIBs;
    std::array<D3D12_VERTEX_BUFFER_VIEW, MESH_LEVELS> m_meshVBViews{};
    std::array<D3D12_INDEX_BUFFER_VIEW, MESH_LEVELS> m_meshIBViews{};
    std::array<UINT, MESH_LEVELS> m_meshIndexCounts{};

    std::unique_ptr<QuadNode> m_root;
    std::vector<const QuadNode*> m_visibleTiles;
    Stats m_stats;

    float m_worldSize = 2000.0f;
    float m_heightScale = 200.0f;
    XMFLOAT3 m_worldOffset = XMFLOAT3(3500.0f, -50.0f, 0.0f);
    float m_minHeight = 0.0f;
    float m_maxHeight = 0.0f;
    float m_fovY = XM_PI / 3.0f;
    UINT m_screenWidth = 1280;
    UINT m_screenHeight = 720;

    UINT m_srvStartSlot = 0;

    bool m_initialized = false;
};