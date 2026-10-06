#include "Terrain.h"
#include "TextureLoader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

static void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr)) throw std::runtime_error("Terrain: DirectX call failed");
}

bool Terrain::Init(ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    ID3D12DescriptorHeap* srvHeap,
    UINT srvStartSlot,
    const std::wstring& heightMapPath,
    const std::wstring& diffuseMapPath,
    float worldSize,
    float heightScale)
{
    m_worldSize = worldSize;
    m_heightScale = heightScale;
    m_srvStartSlot = srvStartSlot;

    m_sseThreshold = 12.0f;
    m_maxDepth = 5;

    if (!LoadHeightMap(device, cmdList, heightMapPath))
    {
        OutputDebugStringA("[TERRAIN] Failed to load heightmap!\n");
        return false;
    }

    if (!LoadDiffuseMap(device, cmdList, diffuseMapPath))
    {
        OutputDebugStringA("[TERRAIN] WARNING: diffuse map failed, using procedural color\n");
    }

    CreateMeshPyramid(device);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    UINT descSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHeight(
        srvHeap->GetCPUDescriptorHandleForHeapStart(),
        srvStartSlot, descSize);
    device->CreateShaderResourceView(m_heightMap.Get(), &srvDesc, srvHeight);

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvDiffuseSimple(
        srvHeap->GetCPUDescriptorHandleForHeapStart(),
        srvStartSlot + 1, descSize);

    if (m_diffuseMap.Get())
    {
        device->CreateShaderResourceView(m_diffuseMap.Get(), &srvDesc, srvDiffuseSimple);
    }
    else
    {
        device->CreateShaderResourceView(m_heightMap.Get(), &srvDesc, srvDiffuseSimple);
    }

    m_root = std::make_unique<QuadNode>();
    m_root->Center = XMFLOAT3(m_worldOffset.x, (m_minHeight + m_maxHeight) * 0.5f + m_worldOffset.y, m_worldOffset.z);
    m_root->Size = m_worldSize;
    m_root->Depth = 0;
    m_root->LOD = m_maxDepth;
    m_root->MeshLevel = MESH_LEVELS - 1;

    float half = m_worldSize * 0.5f;
    m_root->Bounds.Center = m_root->Center;
    m_root->Bounds.Extents = XMFLOAT3(half + 200.0f, (m_maxHeight - m_minHeight) * 0.5f + m_heightScale + 100.0f, half + 200.0f);

    BuildQuadTree(*m_root, 0, m_maxDepth);

    char msg[256];
    sprintf_s(msg, "[TERRAIN] Built quadtree: %d nodes total, maxDepth=%d\n",
        m_stats.totalNodes, m_stats.maxDepth);
    OutputDebugStringA(msg);

    m_initialized = true;
    return true;
}

bool Terrain::LoadHeightMap(ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& path)
{
    TextureLoader::TextureData td;
    if (!TextureLoader::LoadFromFile(path, td))
    {
        OutputDebugStringA("[TERRAIN] TextureLoader::LoadFromFile FAILED\n");
        return false;
    }

    if (!TextureLoader::CreateTexture(device, cmdList, td,
        m_heightMap, m_heightMapUpload))
    {
        OutputDebugStringA("[TERRAIN] TextureLoader::CreateTexture FAILED\n");
        return false;
    }

    m_heightMapWidth = td.width;
    m_heightMapHeight = td.height;

    m_heightData.resize((size_t)m_heightMapWidth * m_heightMapHeight);
    float hMin = 1e30f, hMax = -1e30f;
    for (UINT y = 0; y < m_heightMapHeight; ++y)
    {
        for (UINT x = 0; x < m_heightMapWidth; ++x)
        {
            size_t srcIdx = ((size_t)y * m_heightMapWidth + x) * 4;
            float h = td.pixels[srcIdx] / 255.0f;
            m_heightData[(size_t)y * m_heightMapWidth + x] = h;
            hMin = (std::min)(hMin, h);
            hMax = (std::max)(hMax, h);
        }
    }

    m_minHeight = hMin * m_heightScale;
    m_maxHeight = hMax * m_heightScale;

    std::stringstream ss;
    ss << "[TERRAIN] Heightmap " << m_heightMapWidth << "x" << m_heightMapHeight
        << " loaded, height range [" << m_minHeight << ", " << m_maxHeight << "]\n";
    OutputDebugStringA(ss.str().c_str());

    return true;
}

bool Terrain::LoadDiffuseMap(ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& path)
{
    TextureLoader::TextureData td;
    if (!TextureLoader::LoadFromFile(path, td))
    {
        OutputDebugStringA("[TERRAIN] Diffuse LoadFromFile FAILED\n");
        return false;
    }

    if (!TextureLoader::CreateTexture(device, cmdList, td,
        m_diffuseMap, m_diffuseMapUpload))
    {
        OutputDebugStringA("[TERRAIN] Diffuse CreateTexture FAILED\n");
        return false;
    }

    std::stringstream ss;
    ss << "[TERRAIN] Diffuse map " << td.width << "x" << td.height << " loaded\n";
    OutputDebugStringA(ss.str().c_str());

    return true;
}

float Terrain::SampleHeightCPU(float u, float v) const
{
    u = (std::max)(0.0f, (std::min)(1.0f, u));
    v = (std::max)(0.0f, (std::min)(1.0f, v));

    float fx = u * (m_heightMapWidth - 1);
    float fy = v * (m_heightMapHeight - 1);

    UINT x0 = (UINT)fx;
    UINT y0 = (UINT)fy;
    UINT x1 = (std::min)(x0 + 1, m_heightMapWidth - 1);
    UINT y1 = (std::min)(y0 + 1, m_heightMapHeight - 1);
    float tx = fx - x0;
    float ty = fy - y0;

    float h00 = m_heightData[(size_t)y0 * m_heightMapWidth + x0];
    float h10 = m_heightData[(size_t)y0 * m_heightMapWidth + x1];
    float h01 = m_heightData[(size_t)y1 * m_heightMapWidth + x0];
    float h11 = m_heightData[(size_t)y1 * m_heightMapWidth + x1];

    float h0 = h00 * (1 - tx) + h10 * tx;
    float h1 = h01 * (1 - tx) + h11 * tx;
    return h0 * (1 - ty) + h1 * ty;
}

void Terrain::GetNodeHeightRange(const QuadNode& node,
    float& outMin, float& outMax) const
{
    outMin = 1e30f;
    outMax = -1e30f;

    float half = node.Size * 0.5f;
    const float du[5] = { -half,  half, -half,  half, 0.0f };
    const float dv[5] = { -half, -half,  half,  half, 0.0f };

    for (int i = 0; i < 5; ++i)
    {
        float wx = node.Center.x + du[i] - m_worldOffset.x;
        float wz = node.Center.z + dv[i] - m_worldOffset.z;
        float u = (wx / m_worldSize) + 0.5f;
        float v = (wz / m_worldSize) + 0.5f;

        float h = SampleHeightCPU(u, v) * m_heightScale;
        outMin = (std::min)(outMin, h);
        outMax = (std::max)(outMax, h);
    }
}

void Terrain::BuildQuadTree(QuadNode& node, int depth, int maxDepth)
{
    m_stats.totalNodes++;
    m_stats.maxDepth = (std::max)(m_stats.maxDepth, depth);

    if (depth >= maxDepth)
    {
        node.IsLeaf = true;
        node.MeshLevel = MESH_LEVELS - 1;
        return;
    }

    node.IsLeaf = false;
    node.MeshLevel = (std::min)(depth, MESH_LEVELS - 1);

    float halfSize = node.Size * 0.5f;
    float quarterSize = node.Size * 0.25f;

    for (int i = 0; i < 4; ++i)
    {
        float offsetX = (i & 1) ? quarterSize : -quarterSize;
        float offsetZ = (i & 2) ? quarterSize : -quarterSize;

        auto child = std::make_unique<QuadNode>();
        child->Center = XMFLOAT3(
            node.Center.x + offsetX,
            (m_minHeight + m_maxHeight) * 0.5f + m_worldOffset.y,
            node.Center.z + offsetZ
        );
        child->Size = halfSize;
        child->Depth = depth + 1;
        child->LOD = maxDepth - (depth + 1);
        child->Bounds.Center = child->Center;
        child->Bounds.Extents = XMFLOAT3(
            halfSize * 0.5f + 200.0f, (m_maxHeight - m_minHeight) * 0.5f + m_heightScale + 100.0f,
            halfSize * 0.5f + 200.0f
        );

        BuildQuadTree(*child, depth + 1, maxDepth);
        node.Children[i] = std::move(child);
    }
}

bool Terrain::ShouldSplit(const QuadNode& node,
    const XMMATRIX& viewProj,
    const XMFLOAT3& cameraPos) const
{
    if (node.IsLeaf) return false;

    XMVECTOR cam = XMLoadFloat3(&cameraPos);
    XMVECTOR nodeCenter = XMLoadFloat3(&node.Bounds.Center);
    XMVECTOR nodeExtents = XMLoadFloat3(&node.Bounds.Extents);
    XMVECTOR dVec = XMVectorAbs(cam - nodeCenter) - nodeExtents;

    dVec = XMVectorMax(dVec, XMVectorZero());
    float d = XMVector3Length(dVec).m128_f32[0];

    if (d < 1.0f) return true;

    int meshRes = m_baseMeshRes << node.MeshLevel;
    float segmentSize = node.Size / (float)(meshRes - 1);
    float eps = segmentSize;

    float x = (float)m_screenWidth;
    float tanHalfFov = tanf(m_fovY * 0.5f);
    float rho = (eps * x) / (2.0f * d * tanHalfFov);

    const float SPLIT_THRESHOLD = m_sseThreshold;
    const float MERGE_THRESHOLD = m_sseThreshold * 0.55f;

    return node.IsSplit ? (rho > MERGE_THRESHOLD) : (rho > SPLIT_THRESHOLD);
}

void Terrain::TraverseAndCollect(QuadNode& node,
    const DirectX::BoundingFrustum& frustum,
    const XMMATRIX& viewProj,
    const XMFLOAT3& cameraPos)
{
    DirectX::ContainmentType containment = frustum.Contains(node.Bounds);
    if (containment == DirectX::DISJOINT)
    {
        return; 
    }

    bool shouldSplit = ShouldSplit(node, viewProj, cameraPos);
    node.IsSplit = shouldSplit;

    if (!shouldSplit)
    {
        m_visibleTiles.push_back(&node);
        return;
    }

    for (int i = 0; i < 4; ++i)
    {
        if (node.Children[i])
        {
            TraverseAndCollect(*node.Children[i], frustum, viewProj, cameraPos);
        }
    }
}

void Terrain::Update(const XMMATRIX& view,
    const XMMATRIX& proj,
    const XMFLOAT3& cameraPos,
    UINT screenWidth,
    UINT screenHeight)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    m_visibleTiles.clear();

    XMMATRIX viewProj = view * proj;

    DirectX::BoundingFrustum frustumView;
    DirectX::BoundingFrustum::CreateFromMatrix(frustumView, proj);

    XMMATRIX invView = XMMatrixInverse(nullptr, view);
    DirectX::BoundingFrustum worldFrustum;
    frustumView.Transform(worldFrustum, invView);

    if (m_root)
        TraverseAndCollect(*m_root, worldFrustum, viewProj, cameraPos);

    m_stats.visibleTiles = (int)m_visibleTiles.size();

    static int dbgCounter = 0;
    if (++dbgCounter % 60 == 0) 
    {
        char buf[256];
        sprintf_s(buf, "[TERRAIN] visible=%d, drawn=%d, totalNodes=%d, maxDepth=%d\n",
            m_stats.visibleTiles,
            m_stats.drawnTilesThisFrame,
            m_stats.totalNodes,
            m_stats.maxDepth);
        OutputDebugStringA(buf);
    }
}

void Terrain::Render(ID3D12GraphicsCommandList* cmdList,
    ID3D12RootSignature* rootSig,
    ID3D12PipelineState* pso,
    ID3D12Resource* cbBuffer,
    void* cbMapped,
    const XMMATRIX& view,
    const XMMATRIX& proj,
    UINT cbSlotSize,
    UINT frameIndex,
    float totalTime)
{
    if (!m_initialized || m_visibleTiles.empty())
        return;

    cmdList->SetPipelineState(pso);
    cmdList->SetGraphicsRootSignature(rootSig);

    D3D12_GPU_VIRTUAL_ADDRESS cbBaseAddr = cbBuffer->GetGPUVirtualAddress();

    const UINT CB_TERRAIN_BASE_SLOT = 3000;
    const UINT MAX_TERRAIN_TILES_PER_FRAME = 1024;
    UINT tileIdx = 0;
    m_stats.drawnTilesThisFrame = 0;

    XMMATRIX viewT = XMMatrixTranspose(view);
    XMMATRIX projT = XMMatrixTranspose(proj);

    struct TerrainCB
    {
        XMFLOAT4X4 World;
        XMFLOAT4X4 View;
        XMFLOAT4X4 Proj;
        XMFLOAT4X4 WorldInvTranspose;
        XMFLOAT4   MaterialDiffuse;
        XMFLOAT4   MaterialSpecular;
        int        HasTexture;
        float      TexTilingX;
        float      TexTilingY;
        float      TotalTime;
        float      TexScrollX;
        float      TexScrollY;
        float      HeightScale;
        float      TileWorldSize;
        float      UVMinX;
        float      UVMinY;
        float      UVSize;
        float      CurtainDrop;
        float      TileCenterX;
        float      TileCenterZ;
        float      WorldOffsetY;
        float      Pad0;
    };

    for (const QuadNode* tile : m_visibleTiles)
    {
        if (tileIdx >= MAX_TERRAIN_TILES_PER_FRAME) break;

        UINT slot = CB_TERRAIN_BASE_SLOT + frameIndex * MAX_TERRAIN_TILES_PER_FRAME + tileIdx;

        UINT8* dst = reinterpret_cast<UINT8*>(cbMapped) + slot * cbSlotSize;
        TerrainCB cb = {};

        XMMATRIX world = XMMatrixScaling(tile->Size, 1.0f, tile->Size) *
            XMMatrixTranslation(tile->Center.x, 0.0f, tile->Center.z);
        XMStoreFloat4x4(&cb.World, world);
        XMStoreFloat4x4(&cb.View, viewT);
        XMStoreFloat4x4(&cb.Proj, projT);
        XMStoreFloat4x4(&cb.WorldInvTranspose,
            XMMatrixTranspose(XMMatrixInverse(nullptr, world)));

        cb.MaterialDiffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        cb.MaterialSpecular = XMFLOAT4(0.0f, 0.8f, 1.0f, 0.0f);
        cb.HasTexture = m_diffuseMap.Get() ? 1 : 0;
        cb.TexTilingX = 1.0f;
        cb.TexTilingY = 1.0f;
        cb.TotalTime = totalTime;
        cb.TexScrollX = 0.0f;
        cb.TexScrollY = 0.0f;
        cb.HeightScale = m_heightScale;
        cb.TileWorldSize = tile->Size;

        float uMin = ((tile->Center.x - m_worldOffset.x) - tile->Size * 0.5f) / m_worldSize + 0.5f;
        float vMin = ((tile->Center.z - m_worldOffset.z) - tile->Size * 0.5f) / m_worldSize + 0.5f;
        cb.UVMinX = uMin;
        cb.UVMinY = vMin;
        cb.UVSize = tile->Size / m_worldSize;
        cb.CurtainDrop = m_heightScale * 0.3f;

        cb.TileCenterX = tile->Center.x;
        cb.TileCenterZ = tile->Center.z;
        cb.WorldOffsetY = m_worldOffset.y;
        cb.Pad0 = 0.0f;

        memcpy(dst, &cb, sizeof(TerrainCB));

        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = cbBaseAddr + slot * cbSlotSize;
        cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);

        int ml = tile->MeshLevel;
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetVertexBuffers(0, 1, &m_meshVBViews[ml]);
        cmdList->IASetIndexBuffer(&m_meshIBViews[ml]);
        cmdList->DrawIndexedInstanced(m_meshIndexCounts[ml], 1, 0, 0, 0);

        m_stats.drawnTilesThisFrame++;
        tileIdx++;
    }
}

void Terrain::CreateMeshPyramid(ID3D12Device* device)
{
    for (int lvl = 0; lvl < MESH_LEVELS; ++lvl)
    {
        int res = m_baseMeshRes << lvl;
        bool addCurtains = (lvl > 0); 
        CreateSingleMesh(device, res, addCurtains,
            m_meshVBs[lvl], m_meshIBs[lvl],
            m_meshVBViews[lvl], m_meshIBViews[lvl],
            m_meshIndexCounts[lvl]);
        std::stringstream ss;
        ss << "[TERRAIN] Mesh L" << lvl << ": " << res << "x" << res
            << " (" << m_meshIndexCounts[lvl] / 3 << " triangles)\n";
        OutputDebugStringA(ss.str().c_str());
    }
}

void Terrain::CreateSingleMesh(ID3D12Device* device,
    int res,
    bool addCurtains,
    ComPtr<ID3D12Resource>& outVB,
    ComPtr<ID3D12Resource>& outIB,
    D3D12_VERTEX_BUFFER_VIEW& outVBView,
    D3D12_INDEX_BUFFER_VIEW& outIBView,
    UINT& outIndexCount)
{
    std::vector<TerrainVertex> verts;
    verts.reserve((size_t)res * res);
    for (int z = 0; z < res; ++z)
    {
        for (int x = 0; x < res; ++x)
        {
            float fx = (float)x / (float)(res - 1);
            float fz = (float)z / (float)(res - 1);
            TerrainVertex v;
            v.Position = XMFLOAT3(fx, 0.0f, fz);
            v.TexCoord = XMFLOAT2(fx, fz);
            verts.push_back(v);
        }
    }

    std::vector<UINT> indices;
    indices.reserve((size_t)(res - 1) * (res - 1) * 6);
    for (int z = 0; z < res - 1; ++z)
    {
        for (int x = 0; x < res - 1; ++x)
        {
            UINT i0 = z * res + x;
            UINT i1 = i0 + 1;
            UINT i2 = i0 + res;
            UINT i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }
    }

    std::vector<TerrainVertex> allVerts = verts;
    std::vector<UINT> allIndices = indices;

    if (addCurtains)
    {
        auto buildCurtain = [&](const std::vector<int>& edgeIdx)
            {
                UINT baseTopStart = (UINT)allVerts.size();

                for (int idx : edgeIdx)
                {
                    TerrainVertex v = verts[idx];
                    v.Position.y = 0.0f;
                    v.TexCoord = XMFLOAT2(-1.0f, 0.0f);
                    allVerts.push_back(v);
                }

                for (int idx : edgeIdx)
                {
                    TerrainVertex v = verts[idx];
                    v.Position.y = -1.0f;
                    v.TexCoord = XMFLOAT2(-1.0f, 0.0f);
                    allVerts.push_back(v);
                }

                for (size_t i = 0; i + 1 < edgeIdx.size(); ++i)
                {
                    UINT top0 = baseTopStart + (UINT)i;
                    UINT top1 = baseTopStart + (UINT)i + 1;
                    UINT bot0 = baseTopStart + (UINT)edgeIdx.size() + (UINT)i;
                    UINT bot1 = baseTopStart + (UINT)edgeIdx.size() + (UINT)i + 1;

                    allIndices.push_back(top0); allIndices.push_back(bot0); allIndices.push_back(top1);
                    allIndices.push_back(top1); allIndices.push_back(bot0); allIndices.push_back(bot1);

                    allIndices.push_back(top0); allIndices.push_back(top1); allIndices.push_back(bot0);
                    allIndices.push_back(top1); allIndices.push_back(bot1); allIndices.push_back(bot0);
                }
            };

        {
            std::vector<int> edge;
            for (int x = 0; x < res; ++x) edge.push_back(x);
            buildCurtain(edge);
        }

        {
            std::vector<int> edge;
            for (int x = 0; x < res; ++x) edge.push_back((res - 1) * res + x);
            buildCurtain(edge);
        }

        {
            std::vector<int> edge;
            for (int z = 0; z < res; ++z) edge.push_back(z * res);
            buildCurtain(edge);
        }

        {
            std::vector<int> edge;
            for (int z = 0; z < res; ++z) edge.push_back(z * res + (res - 1));
            buildCurtain(edge);
        }
    }

    outIndexCount = (UINT)allIndices.size();
    UINT vbSize = (UINT)(allVerts.size() * sizeof(TerrainVertex));
    UINT ibSize = (UINT)(allIndices.size() * sizeof(UINT));

    auto uploadBuffer = [&](const void* data, UINT size, ComPtr<ID3D12Resource>& outBuf) {
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&outBuf)));
        void* mapped = nullptr;
        outBuf->Map(0, nullptr, &mapped);
        memcpy(mapped, data, size);
        outBuf->Unmap(0, nullptr);
        };

    uploadBuffer(allVerts.data(), vbSize, outVB);
    uploadBuffer(allIndices.data(), ibSize, outIB);

    outVBView.BufferLocation = outVB->GetGPUVirtualAddress();
    outVBView.SizeInBytes = vbSize;
    outVBView.StrideInBytes = sizeof(TerrainVertex);

    outIBView.BufferLocation = outIB->GetGPUVirtualAddress();
    outIBView.SizeInBytes = ibSize;
    outIBView.Format = DXGI_FORMAT_R32_UINT;
}