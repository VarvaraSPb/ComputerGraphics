#include "RenderingSystem.h"
#include <stdexcept>
#include <cmath>
#include "InputDevice.h"
#ifndef D3D12_BUFFER_UAV_FLAG_APPEND
#define D3D12_BUFFER_UAV_FLAG_APPEND static_cast<D3D12_BUFFER_UAV_FLAGS>(0x00000002)
#endif

static void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("DirectX call failed");
}

RenderingSystem::~RenderingSystem()
{
    //Safe cleanup: if delited (Device Removed), COM calls may padat'
    // Oborachivaem in try-catch, so that the destructor doesn't throw iscluchenia
    if (m_initialized) {
        try { FlushCommandQueue(); }
        catch (...) {}
    }
    if (m_constantBuffer && m_cbMapped) {
        try { m_constantBuffer->Unmap(0, nullptr); }
        catch (...) {}
    }
    if (m_pointLightBuffer && m_pointLightsMapped) {
        try { m_pointLightBuffer->Unmap(0, nullptr); }
        catch (...) {}
    }
    if (m_lightBuffer && m_lightMappedData) {
        try { m_lightBuffer->Unmap(0, nullptr); }
        catch (...) {}
    }
    if (m_shadowCB && m_shadowCBData) {
        try { m_shadowCB->Unmap(0, nullptr); }
        catch (...) {}
    }
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
    try { CoUninitialize(); }
    catch (...) {}
}

bool RenderingSystem::Init(HWND hwnd, int width, int height) {
    m_width = width; m_height = height;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    try {
        CreateDevice();
        CreateCommandObjects();
        CreateSwapChain(hwnd, width, height);
        CreateDescriptorHeaps();

        CreateDefaultTextures();

        CreateRenderTargetViews();
        CreateDepthStencilView();
        CreateFence();
        CompileShaders();
        CreateRootSignature();
        CreatePipelineStateObject();
        CreateCubeGeometry();
        CreateConstantBuffer();

        if (!m_gbuffer.Initialize(m_device.Get(), m_cbvSrvHeap.Get(), width, height)) {
            OutputDebugStringA("GBuffer initialization failed!\n");
            return false;
        }

        CompileGeometryShaders();
        CompileLightingShaders();
        CreateGeometryPassPSO();
        CreateLightingRootSignature();
        CreateLightingPassPSO();
        CompileParticleShaders();
        CreateParticleResources();
        CreateParticleRootSignatures();
        CreateParticlePSOs();
        CreateLightingResources();

        CreateRainLightBuffer();
        CreateRainLightSRV();

        CreateScreenQuad();

        CompileShadowShaders();
        CreateShadowMapRootSignature();
        CreateShadowMapPSO();
        CreateShadowMapResources();

        CreatePostProcessResources();
        CompilePostProcessShaders();
        CreatePostProcessRootSignature();
        CreatePostProcessPSOs();

        ThrowIfFailed(m_cmdList->Close());
        ID3D12CommandList* cmds[] = { m_cmdList.Get() };
        m_cmdQueue->ExecuteCommandLists(1, cmds);
        WaitForGPU();
    }
    catch (const std::exception& e) {
        OutputDebugStringA(e.what());
        return false;
    }

    m_initialized = true;
    return true;
}

void RenderingSystem::CreateDevice() {
#ifdef _DEBUG
    ComPtr<ID3D12Debug> dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        dbg->EnableDebugLayer();
#endif
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)));
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))) break;
    }
    if (!m_device) ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
}

void RenderingSystem::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&q, IID_PPV_ARGS(&m_cmdQueue)));
    for (UINT i = 0; i < FRAME_COUNT; ++i)
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocators[i])));
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_cmdList)));
}

void RenderingSystem::CreateSwapChain(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width = width; sc.Height = height;
    sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc.SampleDesc = { 1, 0 };
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = FRAME_COUNT;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(m_cmdQueue.Get(), hwnd, &sc, nullptr, nullptr, &sc1));
    ThrowIfFailed(sc1.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void RenderingSystem::CreateDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvD{};
    rtvD.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvD.NumDescriptors = FRAME_COUNT + 5;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvD, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvD{};
    dsvD.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvD.NumDescriptors = 1 + MAX_CASCADES; 
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvD, IID_PPV_ARGS(&m_dsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC cbvD{};
    UINT numDescriptors = 150 + (MAX_TEXTURES * 3) + 16 + MAX_CASCADES + 16 + 128;
    cbvD.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvD.NumDescriptors = numDescriptors;
    cbvD.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvD, IID_PPV_ARGS(&m_cbvSrvHeap)));
    m_cbvSrvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    m_shadowMapSRVStart = 150 + (MAX_TEXTURES * 3) + 16;
}

void RenderingSystem::CreateDefaultTextures() {
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = 1; texDesc.Height = 1;
    texDesc.DepthOrArraySize = 1; texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    CD3DX12_HEAP_PROPERTIES heapPropsDef(D3D12_HEAP_TYPE_DEFAULT);
    m_device->CreateCommittedResource(&heapPropsDef, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_defaultDiffuseTex));
    m_device->CreateCommittedResource(&heapPropsDef, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_defaultNormalTex));
    m_device->CreateCommittedResource(&heapPropsDef, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_defaultDisplacementTex));

    CD3DX12_HEAP_PROPERTIES heapPropsUp(D3D12_HEAP_TYPE_UPLOAD);
    UINT64 uploadSize = GetRequiredIntermediateSize(m_defaultDiffuseTex.Get(), 0, 1);
    CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    m_device->CreateCommittedResource(&heapPropsUp, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_defaultDiffuseUpload));
    m_device->CreateCommittedResource(&heapPropsUp, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_defaultNormalUpload));
    m_device->CreateCommittedResource(&heapPropsUp, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_defaultDisplacementUpload));

    uint8_t diffData[4] = { 255, 255, 255, 255 };
    uint8_t normData[4] = { 128, 128, 255, 255 };
    uint8_t dispData[4] = { 128, 128, 128, 255 };

    D3D12_SUBRESOURCE_DATA subD = {}; subD.pData = diffData; subD.RowPitch = 4; subD.SlicePitch = 4;
    D3D12_SUBRESOURCE_DATA subN = {}; subN.pData = normData; subN.RowPitch = 4; subN.SlicePitch = 4;
    D3D12_SUBRESOURCE_DATA subP = {}; subP.pData = dispData; subP.RowPitch = 4; subP.SlicePitch = 4;

    UpdateSubresources(m_cmdList.Get(), m_defaultDiffuseTex.Get(), m_defaultDiffuseUpload.Get(), 0, 0, 1, &subD);
    UpdateSubresources(m_cmdList.Get(), m_defaultNormalTex.Get(), m_defaultNormalUpload.Get(), 0, 0, 1, &subN);
    UpdateSubresources(m_cmdList.Get(), m_defaultDisplacementTex.Get(), m_defaultDisplacementUpload.Get(), 0, 0, 1, &subP);

    CD3DX12_RESOURCE_BARRIER barriers[3] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_defaultDiffuseTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_defaultNormalTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_defaultDisplacementTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };
    m_cmdList->ResourceBarrier(3, barriers);

    CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), 4, m_cbvSrvDescSize);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->CreateShaderResourceView(m_defaultDiffuseTex.Get(), &srvDesc, h);
    h.Offset(1, m_cbvSrvDescSize);
    m_device->CreateShaderResourceView(m_defaultNormalTex.Get(), &srvDesc, h);
    h.Offset(1, m_cbvSrvDescSize);
    m_device->CreateShaderResourceView(m_defaultDisplacementTex.Get(), &srvDesc, h);

    m_currentSrvSlot = 7;
}

void RenderingSystem::CreateRenderTargetViews() {
    CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, h);
        h.Offset(1, m_rtvDescSize);
    }
}

void RenderingSystem::CreateDepthStencilView() {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = m_width; d.Height = m_height;
    d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = DXGI_FORMAT_D32_FLOAT; d.SampleDesc = { 1, 0 };
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv{}; cv.Format = DXGI_FORMAT_D32_FLOAT; cv.DepthStencil.Depth = 1.0f;
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&m_depthStencil)));
    m_device->CreateDepthStencilView(m_depthStencil.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void RenderingSystem::CreateFence() {
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValues[m_frameIndex] = 1;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void RenderingSystem::CompileShaders() {
    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(L"PhongShader.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, &m_vsBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }
    hr = D3DCompileFromFile(L"PhongShader.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, &m_psBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }
}

void RenderingSystem::CompileGeometryShaders() {
    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(L"GeometryPass.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, &m_vsBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"GeometryPass.hlsl", nullptr, nullptr, "HSMain", "hs_5_0", flags, 0, &m_hsBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"GeometryPass.hlsl", nullptr, nullptr, "DSMain", "ds_5_0", flags, 0, &m_dsBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"GeometryPass.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, &m_psBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }
}

void RenderingSystem::CompileLightingShaders() {
    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(L"LightingPass.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, &m_lightingVSBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }
    hr = D3DCompileFromFile(L"LightingPass.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, &m_lightingPSBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }
}

void RenderingSystem::CreateRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

    // params cornevoi signature
    CD3DX12_ROOT_PARAMETER params[2];

    params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        0, 0, D3D12_COMPARISON_FUNC_ALWAYS,
        D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        0.0f, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }
    ThrowIfFailed(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
}

void RenderingSystem::CreateLightingRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4 + MAX_CASCADES, 0);

    CD3DX12_ROOT_PARAMETER params[3] = {};
    params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0, 0, D3D12_COMPARISON_FUNC_ALWAYS, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        0.0f, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC shadowSampler(1,
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0, 0, D3D12_COMPARISON_FUNC_LESS, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
        0.0f, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC samplers[2] = { sampler, shadowSampler };

    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(3, params, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }
    ThrowIfFailed(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_lightingRootSignature)));
}

void RenderingSystem::CreatePipelineStateObject()
{
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { layout, _countof(layout) };
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = { m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize() };
    pso.PS = { m_psBlob->GetBufferPointer(), m_psBlob->GetBufferSize() };

    D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.BlendState = blendDesc;
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc = { 1, 0 };

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)));
}

void RenderingSystem::CreateGeometryPassPSO() {
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { layout, _countof(layout) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize() };
    psoDesc.HS = { m_hsBlob->GetBufferPointer(), m_hsBlob->GetBufferSize() };
    psoDesc.DS = { m_dsBlob->GetBufferPointer(), m_dsBlob->GetBufferSize() };
    psoDesc.PS = { m_psBlob->GetBufferPointer(), m_psBlob->GetBufferSize() };

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    psoDesc.NumRenderTargets = 3;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc = { 1, 0 };

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_geometryPassPSO)));

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_wireframePSO)));
}

void RenderingSystem::CreateLightingPassPSO() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_lightingRootSignature.Get();
    psoDesc.VS = { m_lightingVSBlob->GetBufferPointer(), m_lightingVSBlob->GetBufferSize() };
    psoDesc.PS = { m_lightingPSBlob->GetBufferPointer(), m_lightingPSBlob->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState = blendDesc;

    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; 
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc = { 1, 0 };

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_lightingPassPSO)));
}

void RenderingSystem::CreateCubeGeometry() {
    std::array<Vertex, 24> verts = { {
        { { -1,-1, 1 }, { 0, 0, 1 }, { 0,1 } }, { { 1,-1, 1 }, { 0, 0, 1 }, { 1,1 } }, { { 1, 1, 1 }, { 0, 0, 1 }, { 1,0 } }, { { -1, 1, 1 }, { 0, 0, 1 }, { 0,0 } },
        { { 1,-1,-1 }, { 0, 0,-1 }, { 0,1 } }, { { -1,-1,-1 }, { 0, 0,-1 }, { 1,1 } }, { { -1, 1,-1 }, { 0, 0,-1 }, { 1,0 } }, { { 1, 1,-1 }, { 0, 0,-1 }, { 0,0 } },
        { { -1,-1,-1 }, {-1, 0, 0 }, { 0,1 } }, { { -1,-1, 1 }, {-1, 0, 0 }, { 1,1 } }, { { -1, 1, 1 }, {-1, 0, 0 }, { 1,0 } }, { { -1, 1,-1 }, {-1, 0, 0 }, { 0,0 } },
        { { 1,-1, 1 }, { 1, 0, 0 }, { 0,1 } }, { { 1,-1,-1 }, { 1, 0, 0 }, { 1,1 } }, { { 1, 1,-1 }, { 1, 0, 0 }, { 1,0 } }, { { 1, 1, 1 }, { 1, 0, 0 }, { 0,0 } },
        { { -1, 1, 1 }, { 0, 1, 0 }, { 0,1 } }, { { 1, 1, 1 }, { 0, 1, 0 }, { 1,1 } }, { { 1, 1,-1 }, { 0, 1, 0 }, { 1,0 } }, { { -1, 1,-1 }, { 0, 1, 0 }, { 0,0 } },
        { { -1,-1,-1 }, { 0,-1, 0 }, { 0,1 } }, { { 1,-1,-1 }, { 0,-1, 0 }, { 1,1 } }, { { 1,-1, 1 }, { 0,-1, 0 }, { 1,0 } }, { { -1,-1, 1 }, { 0,-1, 0 }, { 0,0 } },
    } };
    std::array<UINT, 36> idx;
    for (int f = 0; f < 6; ++f) {
        UINT b = f * 4;
        idx[f * 6 + 0] = b + 0; idx[f * 6 + 1] = b + 1; idx[f * 6 + 2] = b + 2;
        idx[f * 6 + 3] = b + 0; idx[f * 6 + 4] = b + 2; idx[f * 6 + 5] = b + 3;
    }
    std::vector<Vertex> v(verts.begin(), verts.end());
    std::vector<UINT> i(idx.begin(), idx.end());
    MeshSubset sub; sub.indexStart = 0; sub.indexCount = 36;
    sub.materialIdx = 0;
    m_subsets = { sub };

    GpuMaterial mat; mat.diffuse = { 1.0f, 0.0f, 1.0f, 1.f };
    mat.specular = { 0.8f, 0.8f, 0.8f, 1.f };
    mat.shininess = 32.f; mat.hasTexture = false;
    m_gpuMaterials = { mat };
    UploadMeshToGpu(v, i);
}

void RenderingSystem::UploadMeshToGpu(const std::vector<Vertex>& verts, const std::vector<UINT>& indices) {
    m_vertexBuffer.Reset(); m_indexBuffer.Reset();
    auto upload = [&](const void* data, UINT sz, ComPtr<ID3D12Resource>& buf) {
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(sz);
        ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf)));
        void* p = nullptr; buf->Map(0, nullptr, &p); memcpy(p, data, sz);
        buf->Unmap(0, nullptr);
        };
    UINT vbSz = (UINT)(verts.size() * sizeof(Vertex));
    UINT ibSz = (UINT)(indices.size() * sizeof(UINT));
    upload(verts.data(), vbSz, m_vertexBuffer);
    upload(indices.data(), ibSz, m_indexBuffer);
    m_vbView = { m_vertexBuffer->GetGPUVirtualAddress(), vbSz, sizeof(Vertex) };
    m_ibView = { m_indexBuffer->GetGPUVirtualAddress(), ibSz, DXGI_FORMAT_R32_UINT };
}

void RenderingSystem::CreateConstantBuffer() {
    m_cbSlotSize = (sizeof(ConstantBufferData) + 255) & ~255;
    UINT totalSize = m_cbSlotSize * (MAX_SUBSETS * FRAME_COUNT + 500); 
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)));
    m_constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_cbMapped));
}

void RenderingSystem::CreateScreenQuad() {
    struct SQV { XMFLOAT3 pos; XMFLOAT2 uv; };
    SQV vertices[] = {
        {XMFLOAT3(-1,-1,0), XMFLOAT2(0,1)},
        {XMFLOAT3(-1, 1,0), XMFLOAT2(0,0)},
        {XMFLOAT3(1,-1,0), XMFLOAT2(1,1)},
        {XMFLOAT3(1, 1,0), XMFLOAT2(1,0)}
    };
    UINT sz = sizeof(vertices);
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(sz);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_screenQuadVB)
    ));
    UINT8* pData;
    ThrowIfFailed(m_screenQuadVB->Map(0, nullptr, reinterpret_cast<void**>(&pData)));
    memcpy(pData, vertices, sz); m_screenQuadVB->Unmap(0, nullptr);
    m_screenQuadVBView = { m_screenQuadVB->GetGPUVirtualAddress(), sz, sizeof(SQV) };
}

bool RenderingSystem::LoadObj(const std::string& path) {
    if (m_initialized) FlushCommandQueue();
    ObjMesh mesh;
    if (!ObjLoader::Load(path, mesh)) return false;

    std::vector<Vertex> verts(mesh.vertices.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        verts[i].Position = mesh.vertices[i].Position;
        verts[i].Normal = mesh.vertices[i].Normal;
        verts[i].TexCoord = mesh.vertices[i].TexCoord;
    }
    m_subsets = mesh.subsets;
    std::string dir; size_t p = path.find_last_of("/\\");
    if (p != std::string::npos) dir = path.substr(0, p + 1);

    ThrowIfFailed(m_cmdAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_cmdList->Reset(m_cmdAllocators[m_frameIndex].Get(), nullptr));

    LoadMaterials(mesh, dir);
    UploadMeshToGpu(verts, mesh.indices);

    m_shadowVB = m_vertexBuffer;
    m_shadowIB = m_indexBuffer;
    m_shadowVbView = m_vbView;
    m_shadowIbView = m_ibView;
    m_shadowSubsets = m_subsets;
    OutputDebugStringA("[SHADOW] Geometry copied for shadow map\n");

    ThrowIfFailed(m_cmdList->Close());
    ID3D12CommandList* cmds[] = { m_cmdList.Get() };
    m_cmdQueue->ExecuteCommandLists(1, cmds);
    WaitForGPU();
    for (auto& mat : m_gpuMaterials) mat.textureUpload.Reset();
    return true;
}

void RenderingSystem::LoadMaterials(const ObjMesh& mesh, const std::string& baseDir) {
    m_gpuMaterials.clear();
    if (mesh.materials.empty()) {
        GpuMaterial def; def.diffuse = { 0.8f,0.8f,0.8f,1.f };
        def.specular = { 0.5f,0.5f,0.5f,1.f };
        def.shininess = 32.f; def.hasTexture = false; m_gpuMaterials.push_back(def); return;
    }
    m_gpuMaterials.resize(mesh.materials.size());
    for (size_t i = 0; i < mesh.materials.size(); ++i) {
        const Material& src = mesh.materials[i];
        GpuMaterial& dst = m_gpuMaterials[i];
        dst.diffuse = src.diffuse; dst.specular = src.specular; dst.shininess = src.shininess;
        if (dst.diffuse.x == 0 && dst.diffuse.y == 0 && dst.diffuse.z == 0) dst.diffuse = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
        dst.srvHeapIndex = m_currentSrvSlot;
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), m_currentSrvSlot, m_cbvSrvDescSize);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        if (!src.diffuseTexture.empty()) {
            std::wstring wpath(baseDir.begin(), baseDir.end());
            std::wstring wtex(src.diffuseTexture.begin(), src.diffuseTexture.end());
            wpath += wtex;

            TextureLoader::TextureData td;
            if (TextureLoader::LoadFromFile(wpath, td) && TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, dst.texture, dst.textureUpload)) {
                srvDesc.Format = td.format;
                m_device->CreateShaderResourceView(dst.texture.Get(), &srvDesc, srvHandle);
                dst.hasTexture = true;
            }
            else {
                m_device->CreateShaderResourceView(m_defaultDiffuseTex.Get(), &srvDesc, srvHandle);
            }
        }
        else {
            m_device->CreateShaderResourceView(m_defaultDiffuseTex.Get(), &srvDesc, srvHandle);
        }
        srvHandle.Offset(1, m_cbvSrvDescSize);
        m_device->CreateShaderResourceView(m_defaultNormalTex.Get(), &srvDesc, srvHandle);
        srvHandle.Offset(1, m_cbvSrvDescSize);
        m_device->CreateShaderResourceView(m_defaultDisplacementTex.Get(), &srvDesc, srvHandle);

        m_currentSrvSlot += 3;
    }
}

bool RenderingSystem::LoadStump(const std::string& path) {
    if (m_initialized) FlushCommandQueue();
    ThrowIfFailed(m_cmdAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_cmdList->Reset(m_cmdAllocators[m_frameIndex].Get(), nullptr));

    ObjMesh mesh;
    if (!ObjLoader::Load(path, mesh)) {
        return false;
    }

    std::vector<Vertex> verts(mesh.vertices.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        verts[i].Position = mesh.vertices[i].Position;
        verts[i].Normal = mesh.vertices[i].Normal;
        verts[i].TexCoord = mesh.vertices[i].TexCoord;
    }
    m_stumpSubsets = mesh.subsets;

    m_stumpMaterials.clear();
    m_stumpMaterials.resize(1);
    GpuMaterial& mat = m_stumpMaterials[0];
    mat.diffuse = { 0.8f, 0.8f, 0.8f, 1.0f };
    mat.specular = { 0.5f, 0.5f, 0.5f, 1.0f };
    mat.shininess = 32.0f;
    mat.srvHeapIndex = m_currentSrvSlot;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_currentSrvSlot,
        m_cbvSrvDescSize
    );
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    // diffuse (BaseColor)
    {
        std::wstring diffPath = L"textures/broken_stump/Broken_Stump_rkswd_High_4K_BaseColor.jpg";
        TextureLoader::TextureData td;
        if (TextureLoader::LoadFromFile(diffPath, td) &&
            TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.texture, mat.textureUpload)) {
            srvDesc.Format = td.format;
            m_device->CreateShaderResourceView(mat.texture.Get(), &srvDesc, srvHandle);
            mat.hasTexture = true;
        }
        else {
            m_device->CreateShaderResourceView(m_defaultDiffuseTex.Get(), &srvDesc, srvHandle);
        }
        srvHandle.Offset(1, m_cbvSrvDescSize);
    }

    // normal map
    {
        std::wstring normPath = L"textures/broken_stump/Broken_Stump_rkswd_High_4K_Normal.jpg";
        TextureLoader::TextureData td;
        if (TextureLoader::LoadFromFile(normPath, td) &&
            TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.normalTexture, mat.normalUpload)) {
            srvDesc.Format = td.format;
            m_device->CreateShaderResourceView(mat.normalTexture.Get(), &srvDesc, srvHandle);
        }
        else {
            m_device->CreateShaderResourceView(m_defaultNormalTex.Get(), &srvDesc, srvHandle);
        }
        srvHandle.Offset(1, m_cbvSrvDescSize);
    }

    // displacement map
    {
        std::wstring dispPath = L"textures/broken_stump/DisplacementMap.png";
        TextureLoader::TextureData td;
        if (TextureLoader::LoadFromFile(dispPath, td) &&
            TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.displacementTexture, mat.displacementUpload)) {
            srvDesc.Format = td.format;
            m_device->CreateShaderResourceView(mat.displacementTexture.Get(), &srvDesc, srvHandle);
            OutputDebugStringA("[LoadStump] Displacement map loaded successfully (PNG)\n");
        }
        else {
            m_device->CreateShaderResourceView(m_defaultDisplacementTex.Get(), &srvDesc, srvHandle);
            OutputDebugStringA("[LoadStump] WARNING: Displacement map FAILED to load, using default (gray=0.5)\n");
        }
    }

    m_currentSrvSlot += 3;

    auto upload = [&](const void* data, UINT sz, ComPtr<ID3D12Resource>& buf) {
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(sz);
        HRESULT hr = m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf));
        if (FAILED(hr)) return false;
        void* p = nullptr;
        buf->Map(0, nullptr, &p);
        memcpy(p, data, sz);
        buf->Unmap(0, nullptr);
        return true;
        };

    UINT vbSz = (UINT)(verts.size() * sizeof(Vertex));
    UINT ibSz = (UINT)(mesh.indices.size() * sizeof(UINT));
    if (!upload(verts.data(), vbSz, m_stumpVertexBuffer)) {
        return false;
    }
    if (!upload(mesh.indices.data(), ibSz, m_stumpIndexBuffer)) {
        return false;
    }

    m_stumpVbView = { m_stumpVertexBuffer->GetGPUVirtualAddress(), vbSz, sizeof(Vertex) };
    m_stumpIbView = { m_stumpIndexBuffer->GetGPUVirtualAddress(), ibSz, DXGI_FORMAT_R32_UINT };

    ThrowIfFailed(m_cmdList->Close());
    ID3D12CommandList* cmds[] = { m_cmdList.Get() };
    m_cmdQueue->ExecuteCommandLists(1, cmds);
    WaitForGPU();

    for (auto& m : m_stumpMaterials) {
        m.textureUpload.Reset();
        m.normalUpload.Reset();
        m.displacementUpload.Reset();
    }

    return true;
}

void RenderingSystem::CreateLightingResources() {
    UINT bufferSize = sizeof(LightBufferData);
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_lightBuffer)));
    m_lightBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_lightMappedData));
    if (m_lightMappedData) memset(m_lightMappedData, 0, sizeof(LightBufferData));
}

void RenderingSystem::CreateRainLightBuffer() {
    const UINT stride = sizeof(PointLight);
    const UINT bufferSize = MAX_RAIN_LIGHTS * stride;
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_pointLightBuffer)));
    m_pointLightBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pointLightsMapped));
    m_rainLights.resize(MAX_RAIN_LIGHTS);
}

void RenderingSystem::CreateRainLightSRV() {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_RAIN_LIGHTS;
    srvDesc.Buffer.StructureByteStride = sizeof(PointLight);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), 3, m_cbvSrvDescSize);
    m_device->CreateShaderResourceView(m_pointLightBuffer.Get(), &srvDesc, srvHandle);
}

void RenderingSystem::UpdateRainLights(float deltaTime) {
    m_spawnTimer += deltaTime;
    if (m_spawnTimer >= m_spawnInterval) {
        m_spawnTimer = 0.f;
        for (auto& light : m_rainLights) {
            if (!light.active) {
                light.data.Position.x = m_spawnAreaMin.x + ((float)rand() / RAND_MAX) * (m_spawnAreaMax.x - m_spawnAreaMin.x);
                light.data.Position.z = m_spawnAreaMin.z + ((float)rand() / RAND_MAX) * (m_spawnAreaMax.z - m_spawnAreaMin.z);
                light.data.Position.y = m_spawnAreaMax.y;
                light.data.Position.w = 12.f;
                light.data.Color = XMFLOAT4(0.7f, 0.8f, 0.9f, 2.5f);
                light.active = true;
                light.velocity = XMFLOAT3(0.f, -80.f, 0.f);
                light.lifeTime = 0.f;
                break;
            }
        }
    }
    m_activeLightCount = 0;
    for (auto& light : m_rainLights) {
        if (!light.active) continue;
        light.data.Position.x += light.velocity.x * deltaTime;
        light.data.Position.y += light.velocity.y * deltaTime;
        light.data.Position.z += light.velocity.z * deltaTime;
        light.lifeTime += deltaTime;
        if (light.data.Position.y <= m_floorY) {
            light.data.Position.y = m_floorY;
            light.velocity = XMFLOAT3(0.f, 0.f, 0.f);
            if (light.lifeTime > 3.5f) {
                light.active = false;
                light.data = {};
            }
        }
        m_activeLightCount++;
    }
    UploadRainLightsToGPU();
}

void RenderingSystem::UploadRainLightsToGPU() {
    if (!m_pointLightsMapped) return;
    UINT idx = 0;
    for (const auto& rl : m_rainLights) {
        if (rl.active && idx < MAX_RAIN_LIGHTS) m_pointLightsMapped[idx++] = rl.data;
    }
    while (idx < MAX_RAIN_LIGHTS) m_pointLightsMapped[idx++] = {};
}

void RenderingSystem::AddLight() {
    if (!m_lightMappedData) return;
    memset(m_lightMappedData, 0, sizeof(LightBufferData));
    m_lightMappedData->DirLightDir = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);  
    m_lightMappedData->DirLightColor = XMFLOAT4(1.0f, 1.0f, 0.8f, 0.5f);
    m_lightMappedData->AmbientColor = XMFLOAT4(0.3f, 0.3f, 0.12f, 0.15f);
    m_lightMappedData->NumSpotLights = 0;
    m_lightMappedData->EyePos = XMFLOAT4(m_eye.x, m_eye.y, m_eye.z, 1.0f);
}

void RenderingSystem::BeginFrame(const float clearColor[4]) {
    ThrowIfFailed(m_cmdAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_cmdList->Reset(m_cmdAllocators[m_frameIndex].Get(), nullptr));
    CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_cmdList->ResourceBarrier(1, &b);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescSize);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    m_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp{ 0, 0, (float)m_width, (float)m_height, 0, 1 };
    D3D12_RECT sc{ 0, 0, m_width, m_height };
    m_cmdList->RSSetViewports(1, &vp);
    m_cmdList->RSSetScissorRects(1, &sc);
}

void RenderingSystem::DrawScene(float totalTime, float deltaTime) {
    XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_eye), XMLoadFloat3(&m_target), XMLoadFloat3(&m_up));
    float aspect = (float)m_width / (float)m_height;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), aspect, 0.1f, 5000.f);
    XMMATRIX viewProj = view * proj;

    if (m_motionBlurCBData) {
        XMStoreFloat4x4(&m_motionBlurCBData->gPrevViewProj, XMMatrixTranspose(m_prevViewProj));
        XMStoreFloat4x4(&m_motionBlurCBData->gCurrViewProj, XMMatrixTranspose(viewProj));
    }
    m_prevViewProj = viewProj;

    UpdateCulling(viewProj);


    UpdateCascades(view, proj, m_lightDir);

    for (int i = 0; i < m_numCascades; ++i) {
        RenderShadowMap(m_cascades[i].ViewProj, i);
    }

    if (m_useDeferredRendering) {
        AddLight();
        RenderGeometryPass(totalTime);
        RenderRocks(totalTime);
        m_gbuffer.TransitionToRead(m_cmdList.Get());
        UpdateRainLights(deltaTime);
        RenderLightingPass();
        UpdateParticles(deltaTime, totalTime);

        XMVECTOR lightPos = XMVectorSet(0.0f, 500.0f, 0.0f, 1.0f);
        XMVECTOR clipPos = XMVector4Transform(lightPos, viewProj);

        float w = XMVectorGetW(clipPos);
        if (w > 0.001f) {
            float x = XMVectorGetX(clipPos) / w;
            float y = XMVectorGetY(clipPos) / w;

        }

        if (m_enableToneMapping) {
            ApplyPostProcessing();
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_frameIndex,
            m_rtvDescSize);
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_gbuffer.GetDSVHandle();
        m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        XMStoreFloat4x4(&m_particleRenderCBData->gView, XMMatrixTranspose(view));
        XMStoreFloat4x4(&m_particleRenderCBData->gProj, XMMatrixTranspose(proj));
        m_particleRenderCBData->gCameraPos = m_eye;

        RenderParticles();

    }
    else {
        RenderForwardPass(totalTime);
    }
}

void RenderingSystem::EndFrame() {
    CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_cmdList->ResourceBarrier(1, &b);
    ThrowIfFailed(m_cmdList->Close());
    ID3D12CommandList* cmds[] = { m_cmdList.Get() };
    m_cmdQueue->ExecuteCommandLists(1, cmds);
    ThrowIfFailed(m_swapChain->Present(1, 0));
    MoveToNextFrame();
}

void RenderingSystem::RenderGeometryPass(float totalTime)
{
    m_gbuffer.TransitionToWrite(m_cmdList.Get());
    float clearColor[4] = { 0, 0, 0, 0 };
    m_gbuffer.Clear(m_cmdList.Get(), clearColor);
    m_gbuffer.Bind(m_cmdList.Get());

    D3D12_VIEWPORT vp{ 0, 0, (float)m_width, (float)m_height, 0, 1 };
    D3D12_RECT sc{ 0, 0, m_width, m_height };
    m_cmdList->RSSetViewports(1, &vp);
    m_cmdList->RSSetScissorRects(1, &sc);

    if (m_wireframeMode) {
        m_cmdList->SetPipelineState(m_wireframePSO.Get());
    }
    else {
        m_cmdList->SetPipelineState(m_geometryPassPSO.Get());
    }

    m_cmdList->SetGraphicsRootSignature(m_rootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);
    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

    // sponza
    m_cmdList->IASetVertexBuffers(0, 1, &m_vbView);
    m_cmdList->IASetIndexBuffer(&m_ibView);

    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_eye), XMLoadFloat3(&m_target), XMLoadFloat3(&m_up));
    float aspect = (float)m_width / (float)m_height;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), aspect, 0.1f, 5000.f);
    XMMATRIX wit = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

    for (UINT subIdx = 0; subIdx < m_subsets.size(); ++subIdx)
    {
        const MeshSubset& sub = m_subsets[subIdx];
        if (sub.indexCount == 0) continue;

        int matIdx = (sub.materialIdx >= 0 && sub.materialIdx < (int)m_gpuMaterials.size()) ? sub.materialIdx : 0;
        const GpuMaterial& mat = m_gpuMaterials.empty() ? GpuMaterial{} : m_gpuMaterials[matIdx];

        UINT slotIdx = m_frameIndex * MAX_SUBSETS + (subIdx % MAX_SUBSETS);
        UINT8* slotPtr = reinterpret_cast<UINT8*>(m_cbMapped) + slotIdx * m_cbSlotSize;
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_constantBuffer->GetGPUVirtualAddress() + slotIdx * m_cbSlotSize;

        ConstantBufferData cb{};
        XMStoreFloat4x4(&cb.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&cb.View, XMMatrixTranspose(view));
        XMStoreFloat4x4(&cb.Proj, XMMatrixTranspose(proj));
        XMStoreFloat4x4(&cb.WorldInvTranspose, XMMatrixTranspose(wit));
        cb.MaterialDiffuse = mat.diffuse;
        cb.MaterialSpecular = mat.specular;
        cb.MaterialSpecular.w = mat.shininess;
        cb.HasTexture = mat.hasTexture ? 1 : 0;
        cb.TexTilingX = m_texTiling.x;
        cb.TexTilingY = m_texTiling.y;
        cb.TexScrollX = m_texScroll.x;
        cb.TexScrollY = m_texScroll.y;
        cb.TotalTime = totalTime;
        cb.EyePosW = m_eye;
        cb.DisplacementScale = 0.0f;
        cb.TessNearDist = m_tesselationNearDist;
        cb.TessFarDist = m_tesselationFarDist;

        memcpy(slotPtr, &cb, sizeof(cb));
        m_cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);

        if (mat.srvHeapIndex >= 0)
        {
            CD3DX12_GPU_DESCRIPTOR_HANDLE srvH(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), mat.srvHeapIndex, m_cbvSrvDescSize);
            m_cmdList->SetGraphicsRootDescriptorTable(1, srvH);
        }
        else
        {
            CD3DX12_GPU_DESCRIPTOR_HANDLE nullH(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 4, m_cbvSrvDescSize);
            m_cmdList->SetGraphicsRootDescriptorTable(1, nullH);
        }
        m_cmdList->DrawIndexedInstanced(sub.indexCount, 1, sub.indexStart, 0, 0);
    }

    // stump
    if (m_stumpVertexBuffer.Get() && !m_stumpSubsets.empty())
    {
        XMFLOAT2 savedTexScroll = m_texScroll;
        m_texScroll = { 0.0f, 0.0f };

        m_cmdList->IASetVertexBuffers(0, 1, &m_stumpVbView);
        m_cmdList->IASetIndexBuffer(&m_stumpIbView);

        XMMATRIX stumpWorld = XMMatrixScaling(500.0f, 500.0f, 500.0f) *
            XMMatrixRotationZ(XMConvertToRadians(-90.0f)) *
            XMMatrixTranslation(1000.0f, 100.0f, 80.0f);
        XMMATRIX stumpWit = XMMatrixTranspose(XMMatrixInverse(nullptr, stumpWorld));

        XMFLOAT3 stumpPosF(1000.0f, 100.0f, 80.0f);
        XMVECTOR stumpPos = XMLoadFloat3(&stumpPosF);

        XMVECTOR eyePos = XMLoadFloat3(&m_eye);
        XMVECTOR distVec = stumpPos - eyePos;
        float distanceToStump = XMVector3Length(distVec).m128_f32[0];

        float minDist = m_tesselationNearDist;
        float maxDist = m_tesselationFarDist;
        float maxTess = 32.0f;
        float minTess = 2.0f;
        float expectedTess = minTess;

        if (distanceToStump < maxDist)
        {
            float tess = maxTess * max(0.0f, min(1.0f, (maxDist - distanceToStump) / (maxDist - minDist)));
            expectedTess = max(minTess, tess);
        }

        static int frameCounter = 0;
        if (++frameCounter % 60 == 0)
        {
            char debugMsg[128];
            sprintf_s(debugMsg, "[TESS] Dist: %.0f | Factor: %.1f | Range: %.0f-%.0f\n",
                distanceToStump, expectedTess, minDist, maxDist);
            //OutputDebugStringA(debugMsg);
        }

        for (UINT subIdx = 0; subIdx < m_stumpSubsets.size(); ++subIdx)
        {
            const MeshSubset& sub = m_stumpSubsets[subIdx];
            if (sub.indexCount == 0) continue;

            UINT slotIdx = m_frameIndex * MAX_SUBSETS + 200 + subIdx;
            if (slotIdx >= MAX_SUBSETS * FRAME_COUNT) slotIdx = 0;

            UINT8* slotPtr = reinterpret_cast<UINT8*>(m_cbMapped) + slotIdx * m_cbSlotSize;
            D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_constantBuffer->GetGPUVirtualAddress() + slotIdx * m_cbSlotSize;

            ConstantBufferData cb{};
            XMStoreFloat4x4(&cb.World, XMMatrixTranspose(stumpWorld));
            XMStoreFloat4x4(&cb.View, XMMatrixTranspose(view));
            XMStoreFloat4x4(&cb.Proj, XMMatrixTranspose(proj));
            XMStoreFloat4x4(&cb.WorldInvTranspose, stumpWit);
            cb.MaterialDiffuse = { 1.0f, 0.0f, 0.0f, 1.0f };
            cb.MaterialSpecular = { 0.5f, 0.5f, 0.5f, 32.0f };
            cb.HasTexture = 1;
            cb.TexTilingX = m_texTiling.x;
            cb.TexTilingY = m_texTiling.y;
            cb.TexScrollX = m_texScroll.x;
            cb.TexScrollY = m_texScroll.y;
            cb.TotalTime = totalTime;
            cb.EyePosW = m_eye;
            cb.DisplacementScale = 15.0f;
            cb.TessNearDist = m_tesselationNearDist;
            cb.TessFarDist = m_tesselationFarDist;

            memcpy(slotPtr, &cb, sizeof(cb));
            m_cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);

            int matIdx = (sub.materialIdx >= 0 && sub.materialIdx < (int)m_stumpMaterials.size()) ? sub.materialIdx : 0;
            const GpuMaterial& mat = m_stumpMaterials.empty() ? GpuMaterial{} : m_stumpMaterials[matIdx];

            if (mat.srvHeapIndex >= 0)
            {
                CD3DX12_GPU_DESCRIPTOR_HANDLE srvH(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), mat.srvHeapIndex, m_cbvSrvDescSize);
                m_cmdList->SetGraphicsRootDescriptorTable(1, srvH);
            }
            else
            {
                CD3DX12_GPU_DESCRIPTOR_HANDLE nullH(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 4, m_cbvSrvDescSize);
                m_cmdList->SetGraphicsRootDescriptorTable(1, nullH);
            }
            m_cmdList->DrawIndexedInstanced(sub.indexCount, 1, sub.indexStart, 0, 0);
        }

        m_texScroll = savedTexScroll;
        m_cmdList->IASetVertexBuffers(0, 1, &m_vbView);
        m_cmdList->IASetIndexBuffer(&m_ibView);
    }
}

void RenderingSystem::RenderLightingPass() {
    CD3DX12_RESOURCE_BARRIER hdrToWrite = CD3DX12_RESOURCE_BARRIER::Transition(
        m_hdrRenderTarget.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_cmdList->ResourceBarrier(1, &hdrToWrite);

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    CD3DX12_CPU_DESCRIPTOR_HANDLE hdrRTV(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_hdrRTVIndex,
        m_rtvDescSize);
    m_cmdList->ClearRenderTargetView(hdrRTV, clearColor, 0, nullptr);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_hdrRTVIndex,
        m_rtvDescSize);
    m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_cmdList->SetPipelineState(m_lightingPassPSO.Get());
    m_cmdList->SetGraphicsRootSignature(m_lightingRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    for (int i = 0; i < m_numCascades; i++) {
        if (m_shadowMaps[i]) {
            char msg[128];
            sprintf_s(msg, "[LIGHTING] Shadow map %d is valid\n", i);
            OutputDebugStringA(msg);
        }
        else {
            OutputDebugStringA("[LIGHTING] WARNING: Shadow map is NULL!\n");
        }
    }

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
        m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        0,
        m_cbvSrvDescSize);
    m_cmdList->SetGraphicsRootDescriptorTable(2, srvHandle);

    // LightingCB
    if (m_lightBuffer) {
        m_cmdList->SetGraphicsRootConstantBufferView(0, m_lightBuffer->GetGPUVirtualAddress());
    }

    // ShadowCB
    if (m_shadowCB) {
        m_cmdList->SetGraphicsRootConstantBufferView(1, m_shadowCB->GetGPUVirtualAddress());
    }
    else {
        OutputDebugStringA("[LIGHTING] WARNING: ShadowCB is NULL!\n");
    }

    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmdList->IASetVertexBuffers(0, 0, nullptr);
    m_cmdList->IASetIndexBuffer(nullptr);
    m_cmdList->DrawInstanced(3, 1, 0, 0);
}

void RenderingSystem::RenderForwardPass(float totalTime) {
    if (!m_pso || m_subsets.empty()) return;
    m_cmdList->SetPipelineState(m_pso.Get());
    m_cmdList->SetGraphicsRootSignature(m_rootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmdList->IASetVertexBuffers(0, 1, &m_vbView);
    m_cmdList->IASetIndexBuffer(&m_ibView);

    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_eye), XMLoadFloat3(&m_target), XMLoadFloat3(&m_up));
    float aspect = (float)m_width / (float)m_height;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), aspect, 0.1f, 5000.f);
    XMMATRIX wit = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

    for (UINT subIdx = 0; subIdx < m_subsets.size(); ++subIdx) {
        const MeshSubset& sub = m_subsets[subIdx];
        if (sub.indexCount == 0) continue;

        int matIdx = (sub.materialIdx >= 0 && sub.materialIdx < (int)m_gpuMaterials.size()) ? sub.materialIdx : 0;
        const GpuMaterial& mat = m_gpuMaterials.empty() ? GpuMaterial{} : m_gpuMaterials[matIdx];

        UINT slotIdx = m_frameIndex * MAX_SUBSETS + (subIdx % MAX_SUBSETS);
        UINT8* slotPtr = reinterpret_cast<UINT8*>(m_cbMapped) + slotIdx * m_cbSlotSize;
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_constantBuffer->GetGPUVirtualAddress() + slotIdx * m_cbSlotSize;

        ConstantBufferData cb{};
        XMStoreFloat4x4(&cb.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&cb.View, XMMatrixTranspose(view));
        XMStoreFloat4x4(&cb.Proj, XMMatrixTranspose(proj));
        XMStoreFloat4x4(&cb.WorldInvTranspose, XMMatrixTranspose(wit));
        cb.MaterialDiffuse = mat.diffuse;
        cb.MaterialSpecular = mat.specular;
        cb.MaterialSpecular.w = mat.shininess;
        cb.HasTexture = mat.hasTexture ? 1 : 0;
        cb.TexTilingX = m_texTiling.x;
        cb.TexTilingY = m_texTiling.y;
        cb.TotalTime = totalTime;
        cb.TexScrollX = m_texScroll.x;
        cb.TexScrollY = m_texScroll.y;
        memcpy(slotPtr, &cb, sizeof(cb));

        m_cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);

        if (mat.srvHeapIndex >= 0) {
            CD3DX12_GPU_DESCRIPTOR_HANDLE srvH(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), mat.srvHeapIndex, m_cbvSrvDescSize);
            m_cmdList->SetGraphicsRootDescriptorTable(1, srvH);
        }
        else {
            CD3DX12_GPU_DESCRIPTOR_HANDLE nullH(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 4, m_cbvSrvDescSize);
            m_cmdList->SetGraphicsRootDescriptorTable(1, nullH);
        }
        m_cmdList->DrawIndexedInstanced(sub.indexCount, 1, sub.indexStart, 0, 0);
    }
}

void RenderingSystem::UpdateCamera(float deltaTime, const InputDevice& input) {
    if (input.IsKeyDown('T')) {
        if (!m_tKeyPressed) {
            m_wireframeMode = !m_wireframeMode;
            m_tKeyPressed = true;
            OutputDebugStringA(m_wireframeMode ? "Wireframe: ON\n" : "Wireframe: OFF\n");
        }
    }
    else {
        m_tKeyPressed = false;
    }

    if (input.IsKeyDown('F')) {
        if (!m_cullKeyPressed) {
            m_cullingMode = (m_cullingMode + 1) % 4;
            m_cullKeyPressed = true;

            const char* modes[] = { "OFF", "FRUSTUM", "FRUSTUM+OCTREE", "HIDDEN" };
            char msg[64];
            sprintf_s(msg, "[CULLING] Mode: %s\n", modes[m_cullingMode]);
            OutputDebugStringA(msg);
        }
    }
    else {
        m_cullKeyPressed = false;
    }

    static float lastPrint = 0;
    if (input.IsKeyDown('1')) {
        m_tesselationNearDist = max(10.0f, m_tesselationNearDist - 10.0f);
        if (m_totalTime - lastPrint > 0.1f) {
            char msg[128];
            sprintf_s(msg, "Tess Near Dist: %.1f\n", m_tesselationNearDist);
            OutputDebugStringA(msg);
            lastPrint = m_totalTime;
        }
    }
    if (input.IsKeyDown('2')) {
        m_tesselationNearDist += 10.0f;
        if (m_totalTime - lastPrint > 0.1f) {
            char msg[128];
            sprintf_s(msg, "Tess Near Dist: %.1f\n", m_tesselationNearDist);
            OutputDebugStringA(msg);
            lastPrint = m_totalTime;
        }
    }
    if (input.IsKeyDown('3')) {
        m_tesselationFarDist = max(100.0f, m_tesselationFarDist - 50.0f);
        if (m_totalTime - lastPrint > 0.1f) {
            char msg[128];
            sprintf_s(msg, "Tess Far Dist: %.1f\n", m_tesselationFarDist);
            OutputDebugStringA(msg);
            lastPrint = m_totalTime;
        }
    }
    if (input.IsKeyDown('4')) {
        m_tesselationFarDist += 50.0f;
        if (m_totalTime - lastPrint > 0.1f) {
            char msg[128];
            sprintf_s(msg, "Tess Far Dist: %.1f\n", m_tesselationFarDist);
            OutputDebugStringA(msg);
            lastPrint = m_totalTime;
        }
    }

    if (input.IsKeyDown('C')) {  
        m_exposure = min(m_exposure + 0.05f, 2.0f);
        char msg[128];
        sprintf_s(msg, "Exposure: %.2f\n", m_exposure);
        OutputDebugStringA(msg);
    }
    if (input.IsKeyDown('Z')) { 
        m_exposure = max(m_exposure - 0.05f, 0.05f);
        char msg[128];
        sprintf_s(msg, "Exposure: %.2f\n", m_exposure);
        OutputDebugStringA(msg);
    }

    if (input.IsKeyDown('B')) {
        if (!m_bKeyPressed) {
            m_enableBloom = !m_enableBloom;
            m_bKeyPressed = true;
            OutputDebugStringA(m_enableBloom ? "Bloom: ON\n" : "Bloom: OFF\n");
        }
    }
    else {
        m_bKeyPressed = false;
    }

    if (input.IsKeyDown('M')) {
        if (!m_mKeyPressed) {
            if (m_motionBlurIntensity > 0.01f) {
                m_motionBlurIntensity = 0.0f;
                OutputDebugStringA("Motion Blur: OFF\n");
            }
            else {
                m_motionBlurIntensity = 0.5f;
                OutputDebugStringA("Motion Blur: ON\n");
            }
            m_mKeyPressed = true;
        }
    }
    else {
        m_mKeyPressed = false;
    }

    if (input.IsKeyDown('N')) {
        if (!m_nKeyPressed) {
            m_enableToneMapping = !m_enableToneMapping;
            m_nKeyPressed = true;
            OutputDebugStringA(m_enableToneMapping ? "Tone Mapping: ON\n" : "Tone Mapping: OFF\n");
        }
    }
    else {
        m_nKeyPressed = false;
    }

    float moveSpeed = m_cameraSpeed * deltaTime;
    XMFLOAT3 moveDelta = { 0, 0, 0 };
    if (input.IsKeyDown('W')) moveDelta.z += moveSpeed; if (input.IsKeyDown('S')) moveDelta.z -= moveSpeed;
    if (input.IsKeyDown('A')) moveDelta.x -= moveSpeed; if (input.IsKeyDown('D')) moveDelta.x += moveSpeed;
    if (input.IsKeyDown('Q')) moveDelta.y -= moveSpeed; if (input.IsKeyDown('E')) moveDelta.y += moveSpeed;
    if (input.MouseDX() != 0 || input.MouseDY() != 0) {
        float mouseSensitivity = 0.005f;
        m_cameraYaw += input.MouseDX() * mouseSensitivity;
        m_cameraPitch += input.MouseDY() * mouseSensitivity;
        if (m_cameraPitch < -XM_PIDIV2 + 0.1f) m_cameraPitch = -XM_PIDIV2 + 0.1f;
        if (m_cameraPitch > XM_PIDIV2 - 0.1f) m_cameraPitch = XM_PIDIV2 - 0.1f;
    }

    float rotateSpeed = 1.0f * deltaTime;
    if (input.IsKeyDown(VK_LEFT)) m_cameraYaw += rotateSpeed;
    if (input.IsKeyDown(VK_RIGHT)) m_cameraYaw -= rotateSpeed;
    if (input.IsKeyDown(VK_UP)) {
        m_cameraPitch += rotateSpeed;
        if (m_cameraPitch > XM_PIDIV2 - 0.1f) m_cameraPitch = XM_PIDIV2 - 0.1f;
    }
    if (input.IsKeyDown(VK_DOWN)) {
        m_cameraPitch -= rotateSpeed;
        if (m_cameraPitch < -XM_PIDIV2 + 0.1f) m_cameraPitch = -XM_PIDIV2 + 0.1f;
    }
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(m_cameraPitch, m_cameraYaw, 0);
    XMVECTOR moveVector = XMLoadFloat3(&moveDelta);
    moveVector = XMVector3TransformNormal(moveVector, rotationMatrix);
    XMVECTOR eyePos = XMLoadFloat3(&m_eye);
    eyePos = eyePos + moveVector;
    XMStoreFloat3(&m_eye, eyePos);
    XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
    forward = XMVector3TransformNormal(forward, rotationMatrix);
    XMVECTOR targetPos = eyePos + forward;
    XMStoreFloat3(&m_target, targetPos);
}

float RenderingSystem::GetVerticalAngle() const {
    XMVECTOR eye = XMLoadFloat3(&m_eye); XMVECTOR target = XMLoadFloat3(&m_target);
    XMVECTOR viewDir = XMVector3Normalize(target - eye); XMFLOAT3 dir; XMStoreFloat3(&dir, viewDir);
    float horizLength = sqrtf(dir.x * dir.x + dir.z * dir.z);
    return atan2f(dir.y, horizLength);
}

void RenderingSystem::OnResize(int width, int height) {
    if (!m_initialized || (m_width == width && m_height == height)) return;
    m_width = width; m_height = height; FlushCommandQueue();
    for (auto& rt : m_renderTargets) rt.Reset(); m_depthStencil.Reset();
    ThrowIfFailed(m_swapChain->ResizeBuffers(FRAME_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    CreateRenderTargetViews(); CreateDepthStencilView();
    m_gbuffer.Initialize(m_device.Get(), m_cbvSrvHeap.Get(), width, height);
}

void RenderingSystem::WaitForGPU() {
    const UINT64 val = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_cmdQueue->Signal(m_fence.Get(), val)); m_fenceValues[m_frameIndex]++;
    if (m_fence->GetCompletedValue() < val) {
        ThrowIfFailed(m_fence->SetEventOnCompletion(val, m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }
}

void RenderingSystem::MoveToNextFrame() {
    const UINT64 cur = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_cmdQueue->Signal(m_fence.Get(), cur));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }
    m_fenceValues[m_frameIndex] = cur + 1;
}

void RenderingSystem::FlushCommandQueue() { WaitForGPU(); }

// rock
void RenderingSystem::LoadRock(const std::string& path) {
    ThrowIfFailed(m_cmdAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_cmdList->Reset(m_cmdAllocators[m_frameIndex].Get(), nullptr));

    ObjMesh mesh;
    if (!ObjLoader::Load(path, mesh)) {
        m_rockBaseBounds = DirectX::BoundingBox(XMFLOAT3(0, 0, 0), XMFLOAT3(2.0f, 1.5f, 2.0f));

        m_rockMaterials.clear();
        GpuMaterial def{};
        def.diffuse = { 0.5f, 0.5f, 0.5f, 1.f };
        def.hasTexture = false;
        def.srvHeapIndex = -1;
        m_rockMaterials.push_back(def);

        ThrowIfFailed(m_cmdList->Close());
        ID3D12CommandList* cmds[] = { m_cmdList.Get() };
        m_cmdQueue->ExecuteCommandLists(1, cmds);
        WaitForGPU();
        return;
    }

    std::vector<Vertex> verts(mesh.vertices.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        verts[i].Position = mesh.vertices[i].Position;
        verts[i].Normal = mesh.vertices[i].Normal;
        verts[i].TexCoord = mesh.vertices[i].TexCoord;
    }
    m_rockSubsets = mesh.subsets;

    m_rockMaterials.clear();
    m_rockMaterials.resize(1);
    GpuMaterial& mat = m_rockMaterials[0];

    mat.diffuse = { 0.8f, 0.8f, 0.8f, 1.0f };
    mat.specular = { 0.5f, 0.5f, 0.5f, 1.0f };
    mat.shininess = 32.0f;
    mat.srvHeapIndex = m_currentSrvSlot;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_currentSrvSlot,
        m_cbvSrvDescSize
    );

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    std::wstring texPath = L"rock/testures/";

    {
        std::wstring albedoPath = texPath + L"Rock_Albedo.png";
        TextureLoader::TextureData td;
        if (TextureLoader::LoadFromFile(albedoPath, td) &&
            TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.texture, mat.textureUpload)) {
            srvDesc.Format = td.format;
            m_device->CreateShaderResourceView(mat.texture.Get(), &srvDesc, srvHandle);
            mat.hasTexture = true;
        }
        else {
            albedoPath = texPath + L"Rock_Albedo.png";
            if (TextureLoader::LoadFromFile(albedoPath, td) &&
                TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.texture, mat.textureUpload)) {
                srvDesc.Format = td.format;
                m_device->CreateShaderResourceView(mat.texture.Get(), &srvDesc, srvHandle);
                mat.hasTexture = true;
            }
            else {
                m_device->CreateShaderResourceView(m_defaultDiffuseTex.Get(), &srvDesc, srvHandle);
            }
        }
        srvHandle.Offset(1, m_cbvSrvDescSize);
    }

    // Normal map
    {
        std::wstring normalPath = texPath + L"Rock_Normal.png";
        TextureLoader::TextureData td;
        if (TextureLoader::LoadFromFile(normalPath, td) &&
            TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.normalTexture, mat.normalUpload)) {
            srvDesc.Format = td.format;
            m_device->CreateShaderResourceView(mat.normalTexture.Get(), &srvDesc, srvHandle);
        }
        else {
            normalPath = texPath + L"Rock_Normal.png";
            if (TextureLoader::LoadFromFile(normalPath, td) &&
                TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.normalTexture, mat.normalUpload)) {
                srvDesc.Format = td.format;
                m_device->CreateShaderResourceView(mat.normalTexture.Get(), &srvDesc, srvHandle);
            }
            else {
                m_device->CreateShaderResourceView(m_defaultNormalTex.Get(), &srvDesc, srvHandle);
            }
        }
        srvHandle.Offset(1, m_cbvSrvDescSize);
    }

    // Displacement/Roughness map
    {
        std::wstring dispPath = texPath + L"Rock_Roughness.png";
        TextureLoader::TextureData td;
        if (TextureLoader::LoadFromFile(dispPath, td) &&
            TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.displacementTexture, mat.displacementUpload)) {
            srvDesc.Format = td.format;
            m_device->CreateShaderResourceView(mat.displacementTexture.Get(), &srvDesc, srvHandle);
        }
        else {
            dispPath = texPath + L"Rock_Roughness.png";
            if (TextureLoader::LoadFromFile(dispPath, td) &&
                TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.displacementTexture, mat.displacementUpload)) {
                srvDesc.Format = td.format;
                m_device->CreateShaderResourceView(mat.displacementTexture.Get(), &srvDesc, srvHandle);
            }
            else {
                dispPath = texPath + L"Rock_Occlusion.png";
                if (TextureLoader::LoadFromFile(dispPath, td) &&
                    TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.displacementTexture, mat.displacementUpload)) {
                    srvDesc.Format = td.format;
                    m_device->CreateShaderResourceView(mat.displacementTexture.Get(), &srvDesc, srvHandle);
                }
                else {
                    m_device->CreateShaderResourceView(m_defaultDisplacementTex.Get(), &srvDesc, srvHandle);
                }
            }
        }
    }

    m_currentSrvSlot += 3;

    if (!verts.empty()) {
        XMFLOAT3 minV = verts[0].Position, maxV = verts[0].Position;
        for (const auto& v : verts) {
            minV.x = min(minV.x, v.Position.x); minV.y = min(minV.y, v.Position.y); minV.z = min(minV.z, v.Position.z);
            maxV.x = max(maxV.x, v.Position.x); maxV.y = max(maxV.y, v.Position.y); maxV.z = max(maxV.z, v.Position.z);
        }
        XMFLOAT3 center = { (minV.x + maxV.x) * 0.5f, (minV.y + maxV.y) * 0.5f, (minV.z + maxV.z) * 0.5f };
        XMFLOAT3 extents = { (maxV.x - minV.x) * 0.5f, (maxV.y - minV.y) * 0.5f, (maxV.z - minV.z) * 0.5f };
        m_rockBaseBounds = DirectX::BoundingBox(center, extents);
    }

    auto uploadBuffer = [&](const void* data, UINT size, ComPtr<ID3D12Resource>& outBuffer) {
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&outBuffer)));
        void* mapped = nullptr;
        outBuffer->Map(0, nullptr, &mapped);
        memcpy(mapped, data, size);
        outBuffer->Unmap(0, nullptr);
        };

    UINT vbSize = (UINT)(verts.size() * sizeof(Vertex));
    UINT ibSize = (UINT)(mesh.indices.size() * sizeof(UINT));
    uploadBuffer(verts.data(), vbSize, m_rockVertexBuffer);
    uploadBuffer(mesh.indices.data(), ibSize, m_rockIndexBuffer);

    m_rockVbView = { m_rockVertexBuffer->GetGPUVirtualAddress(), vbSize, sizeof(Vertex) };
    m_rockIbView = { m_rockIndexBuffer->GetGPUVirtualAddress(), ibSize, DXGI_FORMAT_R32_UINT };

    ThrowIfFailed(m_cmdList->Close());
    ID3D12CommandList* cmds[] = { m_cmdList.Get() };
    m_cmdQueue->ExecuteCommandLists(1, cmds);
    WaitForGPU();

    mat.textureUpload.Reset();
    mat.normalUpload.Reset();
    mat.displacementUpload.Reset();
}

void RenderingSystem::GenerateRocks(int count, float areaRadius) {
    LoadRock("rock/rock.obj");
    m_rocks.clear();
    m_rocks.reserve(count);

    std::mt19937 rng(42);

    std::uniform_real_distribution<float> distX(-areaRadius, areaRadius);
    std::uniform_real_distribution<float> distZ(-areaRadius, areaRadius);
    std::uniform_real_distribution<float> distY(-1.5f, -0.5f);
    std::uniform_real_distribution<float> distScale(0.15f, 0.6f);
    std::uniform_real_distribution<float> distRot(0.0f, XM_PI * 2.0f);

    for (int i = 0; i < count; ++i) {
        float scale = distScale(rng);
        float rot = distRot(rng);

        XMFLOAT3 pos(distX(rng), distY(rng), distZ(rng));

        XMMATRIX world = XMMatrixScaling(scale, scale, scale) *
            XMMatrixRotationY(rot) *
            XMMatrixTranslation(pos.x, pos.y, pos.z);

        RockInstance rock{};
        rock.World = world;

        XMFLOAT3 minPt(
            m_rockBaseBounds.Center.x - m_rockBaseBounds.Extents.x,
            m_rockBaseBounds.Center.y - m_rockBaseBounds.Extents.y,
            m_rockBaseBounds.Center.z - m_rockBaseBounds.Extents.z
        );
        XMFLOAT3 maxPt(
            m_rockBaseBounds.Center.x + m_rockBaseBounds.Extents.x,
            m_rockBaseBounds.Center.y + m_rockBaseBounds.Extents.y,
            m_rockBaseBounds.Center.z + m_rockBaseBounds.Extents.z
        );

        XMVECTOR vertices[8];
        vertices[0] = XMVectorSet(minPt.x, minPt.y, minPt.z, 1.0f);
        vertices[1] = XMVectorSet(minPt.x, minPt.y, maxPt.z, 1.0f);
        vertices[2] = XMVectorSet(minPt.x, maxPt.y, minPt.z, 1.0f);
        vertices[3] = XMVectorSet(minPt.x, maxPt.y, maxPt.z, 1.0f);
        vertices[4] = XMVectorSet(maxPt.x, minPt.y, minPt.z, 1.0f);
        vertices[5] = XMVectorSet(maxPt.x, minPt.y, maxPt.z, 1.0f);
        vertices[6] = XMVectorSet(maxPt.x, maxPt.y, minPt.z, 1.0f);
        vertices[7] = XMVectorSet(maxPt.x, maxPt.y, maxPt.z, 1.0f);

        XMVECTOR vMin = XMVectorReplicate(FLT_MAX);
        XMVECTOR vMax = XMVectorReplicate(-FLT_MAX);

        for (int j = 0; j < 8; ++j) {
            XMVECTOR v = XMVector3Transform(vertices[j], world);
            vMin = XMVectorMin(vMin, v);
            vMax = XMVectorMax(vMax, v);
        }

        rock.Bounds.Center = XMFLOAT3(
            (XMVectorGetX(vMin) + XMVectorGetX(vMax)) * 0.5f,
            (XMVectorGetY(vMin) + XMVectorGetY(vMax)) * 0.5f,
            (XMVectorGetZ(vMin) + XMVectorGetZ(vMax)) * 0.5f
        );
        rock.Bounds.Extents = XMFLOAT3(
            (XMVectorGetX(vMax) - XMVectorGetX(vMin)) * 0.5f,
            (XMVectorGetY(vMax) - XMVectorGetY(vMin)) * 0.5f,
            (XMVectorGetZ(vMax) - XMVectorGetZ(vMin)) * 0.5f
        );

        m_rocks.push_back(rock);
    }

    BuildOctree();
    OutputDebugStringA(("Generated " + std::to_string(count) + " rocks (Wide Field). Octree built.\n").c_str());
}

void RenderingSystem::BuildOctree() {
    if (m_rocks.empty()) return;

    std::vector<size_t> allIndices(m_rocks.size());
    std::iota(allIndices.begin(), allIndices.end(), 0);

    m_octree = std::make_unique<OctreeNode>();
    BuildOctreeRecursive(*m_octree, allIndices, 0, 5);
}

//razbienie ogranichivaushih volumes
void RenderingSystem::BuildOctreeRecursive(OctreeNode& node, const std::vector<size_t>& indices, int depth, int maxDepth) {
    if (indices.empty()) return;

    DirectX::BoundingBox nodeBounds = m_rocks[indices[0]].Bounds;
    for (size_t i = 1; i < indices.size(); ++i) {
        DirectX::BoundingBox::CreateMerged(nodeBounds, nodeBounds, m_rocks[indices[i]].Bounds);
    }

    node.Bounds = nodeBounds;

    if (indices.size() <= 4 || depth >= maxDepth) {
        node.Indices = indices;
        node.IsLeaf = true;
        return;
    }

    node.IsLeaf = false;
    XMFLOAT3 center = nodeBounds.Center;
    XMFLOAT3 minB = { center.x - nodeBounds.Extents.x, center.y - nodeBounds.Extents.y, center.z - nodeBounds.Extents.z };
    XMFLOAT3 maxB = { center.x + nodeBounds.Extents.x, center.y + nodeBounds.Extents.y, center.z + nodeBounds.Extents.z };

    for (int i = 0; i < 8; ++i) {
        XMFLOAT3 childMin = {
            (i & 1) ? center.x : minB.x,
            (i & 2) ? center.y : minB.y,
            (i & 4) ? center.z : minB.z
        };
        XMFLOAT3 childMax = {
            (i & 1) ? maxB.x : center.x,
            (i & 2) ? maxB.y : center.y,
            (i & 4) ? maxB.z : center.z
        };

        DirectX::BoundingBox childBounds{
            XMFLOAT3((childMin.x + childMax.x) * 0.5f, (childMin.y + childMax.y) * 0.5f, (childMin.z + childMax.z) * 0.5f),
            XMFLOAT3((childMax.x - childMin.x) * 0.5f, (childMax.y - childMin.y) * 0.5f, (childMax.z - childMin.z) * 0.5f)
        };

        std::vector<size_t> childIndices;
        for (size_t idx : indices) {
            if (childBounds.Intersects(m_rocks[idx].Bounds) != DirectX::DISJOINT)
                childIndices.push_back(idx);
        }

        if (!childIndices.empty()) {
            node.Children[i] = std::make_unique<OctreeNode>();
            BuildOctreeRecursive(*node.Children[i], childIndices, depth + 1, maxDepth);
        }
    }
}

void RenderingSystem::UpdateCulling(const XMMATRIX& viewProj)
{
    m_visibleRocks.clear();

    if (m_cullingMode == 3) {
        return;
    }

    if (m_cullingMode == 0) {
        m_visibleRocks.reserve(m_rocks.size());
        for (size_t i = 0; i < m_rocks.size(); ++i)
            m_visibleRocks.push_back(i);
        return;
    }

    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), (float)m_width / m_height, 0.1f, 5000.f);
    BoundingFrustum frustumView;
    BoundingFrustum::CreateFromMatrix(frustumView, proj);

    XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_eye), XMLoadFloat3(&m_target), XMLoadFloat3(&m_up));
    XMMATRIX invView = XMMatrixInverse(nullptr, view);

    BoundingFrustum frustumWorld;
    frustumView.Transform(frustumWorld, invView);

    if (m_cullingMode == 1) {
        for (size_t i = 0; i < m_rocks.size(); ++i) {
            if (frustumWorld.Intersects(m_rocks[i].Bounds) != DirectX::DISJOINT) {
                m_visibleRocks.push_back(i);
            }
        }
    }
    else if (m_cullingMode == 2 && m_octree) {
        CullOctreeRecursive(m_octree.get(), frustumWorld);
        std::sort(m_visibleRocks.begin(), m_visibleRocks.end());
        m_visibleRocks.erase(std::unique(m_visibleRocks.begin(), m_visibleRocks.end()), m_visibleRocks.end());
    }
}

void RenderingSystem::CullOctreeRecursive(const OctreeNode* node, const DirectX::BoundingFrustum& frustum)
{
    if (!node) return;

    //if the entire node is outside the camera we skip the entire subtree
    if (frustum.Intersects(node->Bounds) == DirectX::DISJOINT)
        return;

    if (node->IsLeaf)
    {
        for (size_t idx : node->Indices) {
            if (frustum.Intersects(m_rocks[idx].Bounds) != DirectX::DISJOINT) {
                m_visibleRocks.push_back(idx);
            }
        }
    }
    else
    {
        for (const auto& child : node->Children) {
            if (child) {
                CullOctreeRecursive(child.get(), frustum);
            }
        }
    }
}

//HERE I CHANGED FOR ISSUE 2.4
void RenderingSystem::RenderRocks(float totalTime) {
    if (m_rocks.empty() || m_visibleRocks.empty() || !m_rockVertexBuffer.Get()) return;

    XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_eye), XMLoadFloat3(&m_target), XMLoadFloat3(&m_up));
    float aspect = (float)m_width / (float)m_height;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), aspect, 0.1f, 5000.f);
    XMVECTOR eyePos = XMLoadFloat3(&m_eye);

    m_cmdList->SetPipelineState(m_geometryPassPSO.Get());
    m_cmdList->SetGraphicsRootSignature(m_rootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    m_cmdList->IASetVertexBuffers(0, 1, &m_rockVbView);
    m_cmdList->IASetIndexBuffer(&m_rockIbView);

    const UINT ROCK_CBV_OFFSET = MAX_SUBSETS / 2;
    const float LOD_DISTANCE = 250.0f;

    for (size_t i : m_visibleRocks) {
        const auto& rock = m_rocks[i];
        XMVECTOR rockPos = XMLoadFloat3(&rock.Bounds.Center);

        float dist = XMVector3Length(rockPos - eyePos).m128_f32[0];

        XMMATRIX finalWorld = rock.World; 

        if (dist > LOD_DISTANCE) {
            XMVECTOR toCamera = XMVector3Normalize(eyePos - rockPos); 
            XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, toCamera));
            up = XMVector3Cross(toCamera, right); 

            XMMATRIX rot = XMMatrixIdentity();
            rot.r[0] = right;
            rot.r[1] = up;
            rot.r[2] = toCamera * 0.1f; 
            rot.r[3] = XMVectorSet(0, 0, 0, 1);

            finalWorld = rot * XMMatrixTranslationFromVector(rockPos);
        }

        XMMATRIX worldInvTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, finalWorld));
        XMMATRIX worldT = XMMatrixTranspose(finalWorld);

        UINT slotIdx = m_frameIndex * MAX_SUBSETS + ROCK_CBV_OFFSET + (i % (MAX_SUBSETS / 2));
        UINT8* slotPtr = reinterpret_cast<UINT8*>(m_cbMapped) + slotIdx * m_cbSlotSize;
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_constantBuffer->GetGPUVirtualAddress() + slotIdx * m_cbSlotSize;

        ConstantBufferData cb{};
        XMStoreFloat4x4(&cb.World, worldT);
        XMStoreFloat4x4(&cb.View, XMMatrixTranspose(view));
        XMStoreFloat4x4(&cb.Proj, XMMatrixTranspose(proj));
        XMStoreFloat4x4(&cb.WorldInvTranspose, worldInvTranspose);

        cb.MaterialDiffuse = m_rockMaterials[0].diffuse;
        cb.MaterialSpecular = m_rockMaterials[0].specular;
        cb.MaterialSpecular.w = 32.0f;
        cb.HasTexture = m_rockMaterials[0].hasTexture ? 1 : 0;
        cb.TexTilingX = 1.0f;
        cb.TexTilingY = 1.0f;
        cb.TotalTime = totalTime;
        cb.EyePosW = m_eye;

        cb.DisplacementScale = (dist > LOD_DISTANCE) ? 0.0f : 0.0f;

        cb.TessNearDist = m_tesselationNearDist;
        cb.TessFarDist = m_tesselationFarDist;

        memcpy(slotPtr, &cb, sizeof(cb));
        m_cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);

        if (m_rockMaterials.size() > 0 && m_rockMaterials[0].srvHeapIndex >= 0) {
            CD3DX12_GPU_DESCRIPTOR_HANDLE srvH(
                m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                m_rockMaterials[0].srvHeapIndex, m_cbvSrvDescSize);
            m_cmdList->SetGraphicsRootDescriptorTable(1, srvH);
        }

        for (const auto& sub : m_rockSubsets)
            m_cmdList->DrawIndexedInstanced(sub.indexCount, 1, sub.indexStart, 0, 0);
    }
}
//NEXT IS THE PARTICLE LAB - I'M CLOSED

// Particle
void RenderingSystem::CompileParticleShaders() {
    UINT flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    ComPtr<ID3DBlob> err;

    auto compile = [&](const wchar_t* file, const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
        HRESULT hr = D3DCompileFromFile(file, nullptr, nullptr, entry, target, flags, 0, &out, &err);
        if (FAILED(hr)) {
            if (err) OutputDebugStringA((char*)err->GetBufferPointer());
            throw std::runtime_error("Shader compile failed");
        }
        };

    compile(L"ParticleCompute.hlsl", "CSMain", "cs_5_0", m_particleCSBlob);
    compile(L"ParticleRender.hlsl", "VSMain", "vs_5_0", m_particleVSBlob);
    compile(L"ParticleRender.hlsl", "GSMain", "gs_5_0", m_particleGSBlob);
    compile(L"ParticleRender.hlsl", "PSMain", "ps_5_0", m_particlePSBlob);
}

void RenderingSystem::CreateParticleResources()
{
    UINT stride = sizeof(Particle);
    UINT bufferSize = stride * MAX_PARTICLES;

    OutputDebugStringA((std::string("[PARTICLES] Creating resources: stride=") + std::to_string(stride) + ", bufferSize=" + std::to_string(bufferSize) + ", MAX_PARTICLES=" + std::to_string(MAX_PARTICLES) + "\n").c_str());

    auto createUAVBuffer = [&](ComPtr<ID3D12Resource>& outBuf) {
        CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
            bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&outBuf)));
        };

    createUAVBuffer(m_particleBufferAppend);
    createUAVBuffer(m_particleBufferConsume);

    {
        std::vector<Particle> initData(MAX_PARTICLES);
        for (auto& p : initData) {
            p.isActive = 0;
            p.lifetime = 0.0f;
            p.position = { 0, 0, 0 };
            p.velocity = { 0, 0, 0 };
            p.size = 0.0f;
            p.color = { 1, 1, 1, 1 };
        }

        CD3DX12_HEAP_PROPERTIES uploadHp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        ThrowIfFailed(m_device->CreateCommittedResource(&uploadHp, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_particleUploadBuffer)));

        void* mapped = nullptr;
        m_particleUploadBuffer->Map(0, nullptr, &mapped);
        memcpy(mapped, initData.data(), bufferSize);
        m_particleUploadBuffer->Unmap(0, nullptr);

        D3D12_RESOURCE_BARRIER toCopy[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_particleBufferAppend.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(m_particleBufferConsume.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)
        };
        m_cmdList->ResourceBarrier(2, toCopy);
        m_cmdList->CopyBufferRegion(m_particleBufferAppend.Get(), 0, m_particleUploadBuffer.Get(), 0, bufferSize);
        m_cmdList->CopyBufferRegion(m_particleBufferConsume.Get(), 0, m_particleUploadBuffer.Get(), 0, bufferSize);

        D3D12_RESOURCE_BARRIER toUAV[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_particleBufferAppend.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            CD3DX12_RESOURCE_BARRIER::Transition(m_particleBufferConsume.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        };
        m_cmdList->ResourceBarrier(2, toUAV);

        OutputDebugStringA("[PARTICLES] Buffers initialized with inactive particles\n");
    }

    auto createCB = [&](ComPtr<ID3D12Resource>& outBuf, void*& mappedData, UINT size) {
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&outBuf)));
        outBuf->Map(0, nullptr, &mappedData);
        };
    createCB(m_particleUpdateCB, (void*&)m_particleUpdateCBData, sizeof(ParticleUpdateCB));
    createCB(m_particleRenderCB, (void*&)m_particleRenderCBData, sizeof(ParticleRenderCB));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = MAX_PARTICLES;
    uavDesc.Buffer.StructureByteStride = stride;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    CD3DX12_CPU_DESCRIPTOR_HANDLE uavH(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), m_particleDescStart, m_cbvSrvDescSize);

    m_device->CreateUnorderedAccessView(m_particleBufferAppend.Get(), nullptr, &uavDesc, uavH);
    uavH.Offset(1, m_cbvSrvDescSize);
    m_device->CreateUnorderedAccessView(m_particleBufferConsume.Get(), nullptr, &uavDesc, uavH);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
    cbvDesc.BufferLocation = m_particleUpdateCB->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(ParticleUpdateCB);
    uavH.Offset(1, m_cbvSrvDescSize);
    m_device->CreateConstantBufferView(&cbvDesc, uavH);

    cbvDesc.BufferLocation = m_particleRenderCB->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(ParticleRenderCB);
    uavH.Offset(1, m_cbvSrvDescSize);
    m_device->CreateConstantBufferView(&cbvDesc, uavH);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.NumElements = MAX_PARTICLES;
    srvDesc.Buffer.StructureByteStride = stride;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvH(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), m_particleDescStart + 4, m_cbvSrvDescSize);
    m_device->CreateShaderResourceView(m_particleBufferAppend.Get(), &srvDesc, srvH);

    srvH.Offset(1, m_cbvSrvDescSize);
    m_device->CreateShaderResourceView(m_particleBufferConsume.Get(), &srvDesc, srvH);

    OutputDebugStringA("[PARTICLES] Resources & SRVs created successfully\n");
}

void RenderingSystem::CreateParticleRootSignatures()
{
    ComPtr<ID3DBlob> serialized, errors;

    CD3DX12_DESCRIPTOR_RANGE uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);
    CD3DX12_ROOT_PARAMETER compParams[2];
    compParams[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    compParams[1].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);
    CD3DX12_ROOT_SIGNATURE_DESC compRS(2, compParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ThrowIfFailed(D3D12SerializeRootSignature(&compRS, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
    ThrowIfFailed(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_particleComputeRootSig)));

    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); 

    CD3DX12_ROOT_PARAMETER rendParams[2];
    rendParams[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rendParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_ROOT_SIGNATURE_DESC rendRS(2, rendParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ThrowIfFailed(D3D12SerializeRootSignature(&rendRS, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
    ThrowIfFailed(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_particleRenderRootSig)));
}

void RenderingSystem::CreateParticlePSOs() {
    if (!m_particleCSBlob || !m_particleVSBlob) return;

    D3D12_COMPUTE_PIPELINE_STATE_DESC cPSO{};
    cPSO.pRootSignature = m_particleComputeRootSig.Get();
    cPSO.CS = { m_particleCSBlob->GetBufferPointer(), m_particleCSBlob->GetBufferSize() };
    ThrowIfFailed(m_device->CreateComputePipelineState(&cPSO, IID_PPV_ARGS(&m_particleComputePSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gPSO{};

    gPSO.InputLayout = { nullptr, 0 };

    gPSO.pRootSignature = m_particleRenderRootSig.Get();
    gPSO.VS = { m_particleVSBlob->GetBufferPointer(), m_particleVSBlob->GetBufferSize() };
    gPSO.GS = { m_particleGSBlob->GetBufferPointer(), m_particleGSBlob->GetBufferSize() };
    gPSO.PS = { m_particlePSBlob->GetBufferPointer(),  m_particlePSBlob->GetBufferSize() };

    gPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    gPSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    gPSO.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gPSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    gPSO.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    gPSO.DepthStencilState.DepthEnable = TRUE;
    gPSO.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; 
    gPSO.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    gPSO.NumRenderTargets = 1;
    gPSO.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    gPSO.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    gPSO.SampleMask = UINT_MAX; 
    gPSO.SampleDesc = { 1, 0 };

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&gPSO, IID_PPV_ARGS(&m_particleRenderPSO)));
}

void RenderingSystem::UpdateParticles(float deltaTime, float totalTime)
{
    static int frameCount = 0;
    if (++frameCount % 60 == 0) {
        OutputDebugStringA((std::string("[PARTICLES] UpdateParticles called. Frame: ") + std::to_string(frameCount) +
            ", deltaTime: " + std::to_string(deltaTime) + ", totalTime: " + std::to_string(totalTime) + "\n").c_str());
    }

    *m_particleUpdateCBData = {};
    m_particleUpdateCBData->gGravity = { 0.0f, -15.0f, 0.0f };
    m_particleUpdateCBData->gDeltaTime = deltaTime;
    m_particleUpdateCBData->gSpawnRate = 5000.0f;
    m_particleUpdateCBData->gMaxLifetime = 4.0f;
    m_particleUpdateCBData->gSpawnMin = { 17.0f, 46.0f, -6.0f };
    m_particleUpdateCBData->gSpawnMax = { 21.0f, 46.0f, -2.0f };
    m_particleUpdateCBData->gTotalTime = totalTime;
    m_particleUpdateCBData->gWindStrength = 0.3f;
    m_particleUpdateCBData->gWindDirection = { 0.5f, 1.0f, 0.5f };
    m_particleUpdateCBData->gMaxParticles = MAX_PARTICLES;
    m_particleUpdateCBData->gSeed = static_cast<uint32_t>(totalTime * 1000.0f);

    auto& readBuf = m_isAppendActive ? m_particleBufferConsume : m_particleBufferAppend;
    auto& writeBuf = m_isAppendActive ? m_particleBufferAppend : m_particleBufferConsume;

    if (frameCount % 60 == 0) {
        OutputDebugStringA((std::string("[PARTICLES] Buffers: read=") +
            (m_isAppendActive ? "Consume" : "Append") + ", write=" +
            (m_isAppendActive ? "Append" : "Consume") + "\n").c_str());
    }

    D3D12_RESOURCE_BARRIER preBarriers[2] = {
        CD3DX12_RESOURCE_BARRIER::UAV(readBuf.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(writeBuf.Get())
    };
    m_cmdList->ResourceBarrier(2, preBarriers);

    m_cmdList->SetPipelineState(m_particleComputePSO.Get());
    m_cmdList->SetComputeRootSignature(m_particleComputeRootSig.Get());

    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE uavH(
        m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        m_particleDescStart, m_cbvSrvDescSize);

    m_cmdList->SetComputeRootConstantBufferView(0, m_particleUpdateCB->GetGPUVirtualAddress());
    m_cmdList->SetComputeRootDescriptorTable(1, uavH);

    m_cmdList->Dispatch((MAX_PARTICLES + 255) / 256, 1, 1);

    D3D12_RESOURCE_BARRIER postBarrier = CD3DX12_RESOURCE_BARRIER::UAV(writeBuf.Get());
    m_cmdList->ResourceBarrier(1, &postBarrier);

    m_isAppendActive = !m_isAppendActive;

    static bool firstUpdate = true;
    if (firstUpdate) {
        OutputDebugStringA("[PARTICLES] First compute dispatch completed. Particles should be active now!\n");
        firstUpdate = false;
    }
}

void RenderingSystem::RenderParticles() {
    if (m_width <= 0 || m_height <= 0) return;

    auto& readBuf = m_particleBufferConsume;
    int readBufIndex = 1; 

    D3D12_RESOURCE_BARRIER toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
        readBuf.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_cmdList->ResourceBarrier(1, &toSRV);

    m_cmdList->SetPipelineState(m_particleRenderPSO.Get());
    m_cmdList->SetGraphicsRootSignature(m_particleRenderRootSig.Get());

    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    m_cmdList->SetGraphicsRootConstantBufferView(0, m_particleRenderCB->GetGPUVirtualAddress());

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvH(
        m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        m_particleDescStart + 4 + readBufIndex,
        m_cbvSrvDescSize);
    m_cmdList->SetGraphicsRootDescriptorTable(1, srvH);

    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    m_cmdList->DrawInstanced(5000, 1, 0, 0);

    D3D12_RESOURCE_BARRIER toUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        readBuf.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_cmdList->ResourceBarrier(1, &toUAV);
}
//particle closed and me too

//shadows
void RenderingSystem::CompileShadowShaders() {
    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(L"ShadowPass.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, &m_shadowVSBlob, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }

    m_shadowPSBlob = nullptr;
}

void RenderingSystem::CreateShadowMapRootSignature() {
    CD3DX12_ROOT_PARAMETER params[1];
    params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(1, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }
    ThrowIfFailed(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_shadowMapRootSig)));
}

void RenderingSystem::CreateShadowMapPSO() {
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { layout, _countof(layout) };
    psoDesc.pRootSignature = m_shadowMapRootSig.Get();
    psoDesc.VS = { m_shadowVSBlob->GetBufferPointer(), m_shadowVSBlob->GetBufferSize() };
    psoDesc.PS = { nullptr, 0 };

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.DepthBias = 0;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc = { 1, 0 };

    HRESULT hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_shadowMapPSO));
    if (FAILED(hr)) {
        OutputDebugStringA("[ERROR] Failed to create ShadowMap PSO!\n");
        ThrowIfFailed(hr);
    }
}

void RenderingSystem::CreateShadowMapResources() {
    for (UINT i = 0; i < MAX_CASCADES; ++i) {
        D3D12_RESOURCE_DESC shadowDesc = {};
        shadowDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        shadowDesc.Width = SHADOW_MAP_SIZE;
        shadowDesc.Height = SHADOW_MAP_SIZE;
        shadowDesc.DepthOrArraySize = 1;
        shadowDesc.MipLevels = 1;
        shadowDesc.Format = DXGI_FORMAT_D32_FLOAT;
        shadowDesc.SampleDesc.Count = 1;
        shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = DXGI_FORMAT_D32_FLOAT;
        clearVal.DepthStencil.Depth = 1.0f;
        clearVal.DepthStencil.Stencil = 0;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &shadowDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearVal, IID_PPV_ARGS(&m_shadowMaps[i])));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(
            m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),
            1 + i,
            m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));

        m_device->CreateDepthStencilView(
            m_shadowMaps[i].Get(),
            &dsvDesc,
            dsvHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
            m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_shadowMapSRVStart + i,
            m_cbvSrvDescSize);

        m_device->CreateShaderResourceView(
            m_shadowMaps[i].Get(),
            &srvDesc,
            srvHandle);
    }

    UINT cbSize = (sizeof(ShadowMapCBData) + 255) & ~255;
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_shadowMapCB)));
    m_shadowMapCB->Map(0, nullptr, reinterpret_cast<void**>(&m_shadowMapCBData));

    UINT shadowCBSize = (sizeof(ShadowCBData) + 255) & ~255;
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(shadowCBSize);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &hp,
        D3D12_HEAP_FLAG_NONE,
        &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_shadowCB)));
    m_shadowCB->Map(0, nullptr, reinterpret_cast<void**>(&m_shadowCBData));

    if (m_shadowCBData) {
        memset(m_shadowCBData, 0, sizeof(ShadowCBData));
        m_shadowCBData->ShadowMapSize = XMFLOAT4(
            (float)SHADOW_MAP_SIZE,
            (float)SHADOW_MAP_SIZE,
            1.0f / (float)SHADOW_MAP_SIZE,
            1.0f / (float)SHADOW_MAP_SIZE);
        m_shadowCBData->ShadowBias = m_shadowBias;
        m_shadowCBData->PCFRadius = m_pcfRadius;
    }
}

void RenderingSystem::RenderShadowMap(const XMMATRIX& lightViewProj, int cascadeIndex) {
    if (cascadeIndex >= MAX_CASCADES || !m_shadowMaps[cascadeIndex]) return;

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadowMaps[cascadeIndex].Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    m_cmdList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),
        1 + cascadeIndex,
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));

    m_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp = { 0, 0, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE, 0, 1 };
    D3D12_RECT sc = { 0, 0, (int)SHADOW_MAP_SIZE, (int)SHADOW_MAP_SIZE };

    m_cmdList->RSSetViewports(1, &vp);
    m_cmdList->RSSetScissorRects(1, &sc);
    m_cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    m_cmdList->SetPipelineState(m_shadowMapPSO.Get());
    m_cmdList->SetGraphicsRootSignature(m_shadowMapRootSig.Get());

    if (m_shadowMapCB) {
        m_cmdList->SetGraphicsRootConstantBufferView(0, m_shadowMapCB->GetGPUVirtualAddress());
    }

    RenderGeometryForShadowMap(lightViewProj);

    CD3DX12_RESOURCE_BARRIER barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadowMaps[cascadeIndex].Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_cmdList->ResourceBarrier(1, &barrier2);
}

void RenderingSystem::RenderGeometryForShadowMap(const XMMATRIX& viewProj) {
    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmdList->IASetVertexBuffers(0, 1, &m_vbView);
    m_cmdList->IASetIndexBuffer(&m_ibView);

    XMMATRIX world = XMMatrixScaling(100.0f, 100.0f, 100.0f) * XMMatrixTranslation(0.0f, 50.0f, 0.0f);
    XMMATRIX worldViewProj = world * viewProj;

    if (m_shadowMapCBData) {
        XMStoreFloat4x4(&m_shadowMapCBData->WorldViewProj, XMMatrixTranspose(worldViewProj));
        m_cmdList->SetGraphicsRootConstantBufferView(0, m_shadowMapCB->GetGPUVirtualAddress());
    }

    m_cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);
}

void RenderingSystem::UpdateCascades(const XMMATRIX& view, const XMMATRIX& proj, const XMFLOAT3& lightDir) {
    XMVECTOR lightPos = XMVectorSet(0.0f, 800.0f, 0.0f, 1.0f);
    XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR upVec = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, upVec);

    float size = 4000.0f;
    float nearZ = 0.1f;
    float farZ = 8000.0f;

    XMMATRIX lightProj = XMMatrixOrthographicLH(size, size, nearZ, farZ);
    XMMATRIX viewProj = lightView * lightProj;

    float maxDist = 2000.0f;

    for (int i = 0; i < m_numCascades; ++i) {
        float t = (float)(i + 1) / (float)m_numCascades;
        float splitDist = maxDist * t * t * 1.5f;
        m_cascades[i].SplitDistance = splitDist;
        m_cascadeSplits[i] = splitDist;
        m_cascades[i].ViewProj = viewProj;
    }

    UpdateShadowConstantBuffer();
}

void RenderingSystem::UpdateShadowConstantBuffer() {
    if (!m_shadowCBData) return;

    for (int i = 0; i < m_numCascades; ++i) {
        XMStoreFloat4x4(&m_shadowCBData->LightViewProj[i], XMMatrixTranspose(m_cascades[i].ViewProj));
    }

    XMFLOAT4 splits;
    splits.x = (m_numCascades > 0) ? m_cascadeSplits[0] : 100.0f;
    splits.y = (m_numCascades > 1) ? m_cascadeSplits[1] : 200.0f;
    splits.z = (m_numCascades > 2) ? m_cascadeSplits[2] : 400.0f;
    splits.w = (m_numCascades > 3) ? m_cascadeSplits[3] : 500.0f;
    m_shadowCBData->CascadeSplits = splits;

    XMVECTOR lightDirVec = XMVector3Normalize(XMLoadFloat3(&m_lightDir));
    XMFLOAT3 lightDir;
    XMStoreFloat3(&lightDir, lightDirVec);
    m_shadowCBData->LightDir = XMFLOAT4(lightDir.x, lightDir.y, lightDir.z, 0.0f);

    XMVECTOR lightPos = -lightDirVec * 1000.0f;
    XMFLOAT3 lightPosF;
    XMStoreFloat3(&lightPosF, lightPos);
    m_shadowCBData->LightPos = XMFLOAT4(lightPosF.x, lightPosF.y, lightPosF.z, 1.0f);

    m_shadowCBData->ShadowMapSize = XMFLOAT4(
        (float)SHADOW_MAP_SIZE,
        (float)SHADOW_MAP_SIZE,
        1.0f / (float)SHADOW_MAP_SIZE,
        1.0f / (float)SHADOW_MAP_SIZE);
    m_shadowCBData->ShadowBias = m_shadowBias;
    m_shadowCBData->PCFRadius = m_pcfRadius;
}
// end of shadows

// post process

void RenderingSystem::CreatePostProcessResources()
{
    OutputDebugStringA("[POST-PROCESS] Creating resources...\n");

    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC hdrDesc = {};
    hdrDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    hdrDesc.Width = m_width;
    hdrDesc.Height = m_height;
    hdrDesc.DepthOrArraySize = 1;
    hdrDesc.MipLevels = 1;
    hdrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrDesc.SampleDesc.Count = 1;
    hdrDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clearVal.Color[0] = 0.0f;
    clearVal.Color[1] = 0.0f;
    clearVal.Color[2] = 0.0f;
    clearVal.Color[3] = 1.0f;

    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &hdrDesc,
        D3D12_RESOURCE_STATE_COMMON, &clearVal, IID_PPV_ARGS(&m_hdrRenderTarget)));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_hdrRTVIndex,
        m_rtvDescSize);
    m_device->CreateRenderTargetView(m_hdrRenderTarget.Get(), &rtvDesc, rtvHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        POST_PROCESS_DESC_START,
        m_cbvSrvDescSize);
    m_device->CreateShaderResourceView(m_hdrRenderTarget.Get(), &srvDesc, srvHandle);

    D3D12_RESOURCE_DESC lumDesc = {};
    lumDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    lumDesc.Width = 1;
    lumDesc.Height = 1;
    lumDesc.DepthOrArraySize = 1;
    lumDesc.MipLevels = 1;
    lumDesc.Format = DXGI_FORMAT_R32_FLOAT;
    lumDesc.SampleDesc.Count = 1;
    lumDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE lumClear = {};
    lumClear.Format = DXGI_FORMAT_R32_FLOAT;
    lumClear.Color[0] = 0.0f;

    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &lumDesc,
        D3D12_RESOURCE_STATE_COMMON, &lumClear, IID_PPV_ARGS(&m_luminanceTexture)));

    D3D12_RENDER_TARGET_VIEW_DESC lumRtvDesc = {};
    lumRtvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    lumRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    lumRtvDesc.Texture2D.MipSlice = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE lumRtvHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_lumRTVIndex,
        m_rtvDescSize);
    m_device->CreateRenderTargetView(m_luminanceTexture.Get(), &lumRtvDesc, lumRtvHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC lumSrvDesc = {};
    lumSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    lumSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    lumSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    lumSrvDesc.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE lumSrvHandle(
        m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        POST_PROCESS_DESC_START + 1,
        m_cbvSrvDescSize);
    m_device->CreateShaderResourceView(m_luminanceTexture.Get(), &lumSrvDesc, lumSrvHandle);

    D3D12_RESOURCE_DESC blurDesc = hdrDesc;
    D3D12_CLEAR_VALUE blurClear = {};
    blurClear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    blurClear.Color[0] = 0.0f;
    blurClear.Color[1] = 0.0f;
    blurClear.Color[2] = 0.0f;
    blurClear.Color[3] = 1.0f;

    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &blurDesc,
        D3D12_RESOURCE_STATE_COMMON, &blurClear, IID_PPV_ARGS(&m_tempBlurTexture)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE blurRtvHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_blurRTVIndex,
        m_rtvDescSize);
    m_device->CreateRenderTargetView(m_tempBlurTexture.Get(), &rtvDesc, blurRtvHandle);

    CD3DX12_CPU_DESCRIPTOR_HANDLE blurSrvHandle(
        m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        POST_PROCESS_DESC_START + 2,
        m_cbvSrvDescSize);
    m_device->CreateShaderResourceView(m_tempBlurTexture.Get(), &srvDesc, blurSrvHandle);

    D3D12_CLEAR_VALUE bloomClear = {};
    bloomClear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    bloomClear.Color[0] = 0.0f;
    bloomClear.Color[1] = 0.0f;
    bloomClear.Color[2] = 0.0f;
    bloomClear.Color[3] = 1.0f;

    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &blurDesc,
        D3D12_RESOURCE_STATE_COMMON, &bloomClear, IID_PPV_ARGS(&m_bloomTexture)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE bloomRtvHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_bloomRTVIndex,
        m_rtvDescSize);
    m_device->CreateRenderTargetView(m_bloomTexture.Get(), &rtvDesc, bloomRtvHandle);

    CD3DX12_CPU_DESCRIPTOR_HANDLE bloomSrvHandle(
        m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_bloomSRVIndex,
        m_cbvSrvDescSize);
    m_device->CreateShaderResourceView(m_bloomTexture.Get(), &srvDesc, bloomSrvHandle);

    OutputDebugStringA("[POST-PROCESS] Resources created successfully\n");
}

void RenderingSystem::CompilePostProcessShaders()
{
    OutputDebugStringA("[POST-PROCESS] Compiling shaders...\n");

    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(L"PostProcess.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, &m_postProcessVSBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"PostProcess.hlsl", nullptr, nullptr, "LuminancePS", "ps_5_0", flags, 0, &m_luminancePSBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"PostProcess.hlsl", nullptr, nullptr, "ToneMapPS", "ps_5_0", flags, 0, &m_toneMapPSBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"PostProcess.hlsl", nullptr, nullptr, "BlurHorizontalPS", "ps_5_0", flags, 0, &m_blurHPSBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"PostProcess.hlsl", nullptr, nullptr, "BlurVerticalPS", "ps_5_0", flags, 0, &m_blurVPSBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"PostProcess.hlsl", nullptr, nullptr, "BloomExtractPS", "ps_5_0", flags, 0, &m_bloomExtractBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"PostProcess.hlsl", nullptr, nullptr, "BloomCombinePS", "ps_5_0", flags, 0, &m_bloomCombineBlob, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }

    hr = D3DCompileFromFile(L"PostProcess.hlsl", nullptr, nullptr, "MotionBlurPS", "ps_5_0", flags, 0, &m_motionBlurBlob, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }
    OutputDebugStringA("[POST-PROCESS] MotionBlurPS compiled\n");

    hr = D3DCompileFromFile(L"PostProcess.hlsl", nullptr, nullptr, "CombinedPostEffectsPS", "ps_5_0", flags, 0, &m_combinedPostEffectsBlob, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }
    OutputDebugStringA("[POST-PROCESS] CombinedPostEffectsPS compiled\n");

    OutputDebugStringA("[POST-PROCESS] Shaders compiled successfully\n");
}

void RenderingSystem::CreatePostProcessRootSignature()
{
    OutputDebugStringA("[POST-PROCESS] Creating root signature...\n");

    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0); 

    CD3DX12_ROOT_PARAMETER params[3];
    params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);  
    params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL); 
    params[2].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    samplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(3, params, 2, samplers,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }
    ThrowIfFailed(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_postProcessRootSig)));

    OutputDebugStringA("[POST-PROCESS] Root signature created\n");
}

void RenderingSystem::CreatePostProcessPSOs()
{
    OutputDebugStringA("[POST-PROCESS] Creating PSOs...\n");

    UINT cbSize = (sizeof(PostProcessConstants) + 255) & ~255;
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(cbSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_postProcessCB)));
    m_postProcessCB->Map(0, nullptr, reinterpret_cast<void**>(&m_postProcessCBData));

    if (m_postProcessCBData) {
        m_postProcessCBData->gExposure = m_exposure;
        m_postProcessCBData->gAdaptationSpeed = 0.3f;
        m_postProcessCBData->gMiddleGray = 0.72f;
        m_postProcessCBData->gLumWhite = 1.5f;
        m_postProcessCBData->gDeltaTime = 0.016f;
        m_postProcessCBData->gMotionBlurIntensity = 0.5f;
        m_postProcessCBData->gMotionBlurSamples = 12.0f;
    }

    UINT motionBlurCbSize = (sizeof(MotionBlurConstants) + 255) & ~255;
    CD3DX12_RESOURCE_DESC motionBlurRd = CD3DX12_RESOURCE_DESC::Buffer(motionBlurCbSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &motionBlurRd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_motionBlurCB)));
    m_motionBlurCB->Map(0, nullptr, reinterpret_cast<void**>(&m_motionBlurCBData));

    if (m_motionBlurCBData) {
        XMStoreFloat4x4(&m_motionBlurCBData->gPrevViewProj, XMMatrixIdentity());
        XMStoreFloat4x4(&m_motionBlurCBData->gCurrViewProj, XMMatrixIdentity());
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_postProcessRootSig.Get();
    psoDesc.VS = { m_postProcessVSBlob->GetBufferPointer(), m_postProcessVSBlob->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc = { 1, 0 };
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
    psoDesc.PS = { m_luminancePSBlob->GetBufferPointer(), m_luminancePSBlob->GetBufferSize() };
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_luminancePSO)));

    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.PS = { m_toneMapPSBlob->GetBufferPointer(), m_toneMapPSBlob->GetBufferSize() };
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_toneMapPSO)));

    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.PS = { m_blurHPSBlob->GetBufferPointer(), m_blurHPSBlob->GetBufferSize() };
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_blurHorizontalPSO)));

    psoDesc.PS = { m_blurVPSBlob->GetBufferPointer(), m_blurVPSBlob->GetBufferSize() };
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_blurVerticalPSO)));

    psoDesc.PS = { m_bloomExtractBlob->GetBufferPointer(), m_bloomExtractBlob->GetBufferSize() };
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_bloomExtractPSO)));

    psoDesc.PS = { m_bloomCombineBlob->GetBufferPointer(), m_bloomCombineBlob->GetBufferSize() };
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_bloomCombinePSO)));

    if (m_motionBlurBlob) {
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.PS = { m_motionBlurBlob->GetBufferPointer(), m_motionBlurBlob->GetBufferSize() };
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_motionBlurPSO)));
        OutputDebugStringA("[POST-PROCESS] Motion Blur PSO created\n");
    }
    else {
        OutputDebugStringA("[POST-PROCESS] WARNING: m_motionBlurBlob is NULL!\n");
    }

    if (m_combinedPostEffectsBlob) {
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.PS = { m_combinedPostEffectsBlob->GetBufferPointer(), m_combinedPostEffectsBlob->GetBufferSize() };
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_combinedPostEffectsPSO)));
        OutputDebugStringA("[POST-PROCESS] Combined Post-Effects PSO created\n");
    }
    else {
        OutputDebugStringA("[POST-PROCESS] WARNING: m_combinedPostEffectsBlob is NULL!\n");
    }

    OutputDebugStringA("[POST-PROCESS] PSOs created\n");
}

void RenderingSystem::ApplyPostProcessing()
{
    if (!m_enableToneMapping) return;

    OutputDebugStringA("[POST-PROCESS] Applying effects...\n");

    if (m_postProcessCBData) {
        m_postProcessCBData->gExposure = m_exposure;
        m_postProcessCBData->gAdaptationSpeed = 0.0f;
        m_postProcessCBData->gMiddleGray = 0.72f;
        m_postProcessCBData->gLumWhite = 1.5f;
        m_postProcessCBData->gDeltaTime = 0.016f;
        m_postProcessCBData->gMotionBlurIntensity = m_motionBlurIntensity;
        m_postProcessCBData->gMotionBlurSamples = m_motionBlurSamples;
    }

    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    CD3DX12_RESOURCE_BARRIER hdrToRead = CD3DX12_RESOURCE_BARRIER::Transition(
        m_hdrRenderTarget.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_cmdList->ResourceBarrier(1, &hdrToRead);

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
        m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        POST_PROCESS_DESC_START,
        m_cbvSrvDescSize);

    float clearColorHDR[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float clearColorLum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (m_enableBloom) {
        CD3DX12_RESOURCE_BARRIER bloomToWrite = CD3DX12_RESOURCE_BARRIER::Transition(
            m_bloomTexture.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_cmdList->ResourceBarrier(1, &bloomToWrite);

        CD3DX12_CPU_DESCRIPTOR_HANDLE bloomRTV(
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_bloomRTVIndex,
            m_rtvDescSize);
        m_cmdList->ClearRenderTargetView(bloomRTV, clearColorHDR, 0, nullptr);
        m_cmdList->OMSetRenderTargets(1, &bloomRTV, FALSE, nullptr);

        m_cmdList->SetPipelineState(m_bloomExtractPSO.Get());
        m_cmdList->SetGraphicsRootSignature(m_postProcessRootSig.Get());
        m_cmdList->SetGraphicsRootConstantBufferView(0, m_postProcessCB->GetGPUVirtualAddress());
        m_cmdList->SetGraphicsRootDescriptorTable(2, srvHandle);
        m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_cmdList->DrawInstanced(3, 1, 0, 0);

        CD3DX12_RESOURCE_BARRIER bloomToRead = CD3DX12_RESOURCE_BARRIER::Transition(
            m_bloomTexture.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_cmdList->ResourceBarrier(1, &bloomToRead);

        CD3DX12_RESOURCE_BARRIER tempToWrite = CD3DX12_RESOURCE_BARRIER::Transition(
            m_tempBlurTexture.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_cmdList->ResourceBarrier(1, &tempToWrite);

        CD3DX12_CPU_DESCRIPTOR_HANDLE blurRTV(
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_blurRTVIndex,
            m_rtvDescSize);
        m_cmdList->ClearRenderTargetView(blurRTV, clearColorHDR, 0, nullptr);
        m_cmdList->OMSetRenderTargets(1, &blurRTV, FALSE, nullptr);
        m_cmdList->SetPipelineState(m_blurHorizontalPSO.Get());

        CD3DX12_GPU_DESCRIPTOR_HANDLE bloomSrvHandle(
            m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            m_bloomSRVIndex,
            m_cbvSrvDescSize);
        m_cmdList->SetGraphicsRootDescriptorTable(2, bloomSrvHandle);
        m_cmdList->DrawInstanced(3, 1, 0, 0);

        CD3DX12_RESOURCE_BARRIER tempToRead = CD3DX12_RESOURCE_BARRIER::Transition(
            m_tempBlurTexture.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_cmdList->ResourceBarrier(1, &tempToRead);

        CD3DX12_RESOURCE_BARRIER bloomToWrite2 = CD3DX12_RESOURCE_BARRIER::Transition(
            m_bloomTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_cmdList->ResourceBarrier(1, &bloomToWrite2);

        m_cmdList->ClearRenderTargetView(bloomRTV, clearColorHDR, 0, nullptr);
        m_cmdList->OMSetRenderTargets(1, &bloomRTV, FALSE, nullptr);
        m_cmdList->SetPipelineState(m_blurVerticalPSO.Get());

        CD3DX12_GPU_DESCRIPTOR_HANDLE tempSrvHandle(
            m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            POST_PROCESS_DESC_START + 2,
            m_cbvSrvDescSize);
        m_cmdList->SetGraphicsRootDescriptorTable(2, tempSrvHandle);
        m_cmdList->DrawInstanced(3, 1, 0, 0);

        CD3DX12_RESOURCE_BARRIER bloomToRead2 = CD3DX12_RESOURCE_BARRIER::Transition(
            m_bloomTexture.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_cmdList->ResourceBarrier(1, &bloomToRead2);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_frameIndex,
            m_rtvDescSize);
        m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        if (m_motionBlurIntensity > 0.01f && m_enableBloom) {
            if (m_combinedPostEffectsPSO) {
                m_cmdList->SetPipelineState(m_combinedPostEffectsPSO.Get());
                m_cmdList->SetGraphicsRootConstantBufferView(1, m_motionBlurCB->GetGPUVirtualAddress());
            }
        }
        else if (m_motionBlurIntensity > 0.01f) {
            if (m_motionBlurPSO) {
                m_cmdList->SetPipelineState(m_motionBlurPSO.Get());
                m_cmdList->SetGraphicsRootConstantBufferView(1, m_motionBlurCB->GetGPUVirtualAddress());
            }
        }
        else {
            m_cmdList->SetPipelineState(m_bloomCombinePSO.Get());
        }

        m_cmdList->SetGraphicsRootSignature(m_postProcessRootSig.Get());
        m_cmdList->SetGraphicsRootConstantBufferView(0, m_postProcessCB->GetGPUVirtualAddress());
        m_cmdList->SetGraphicsRootDescriptorTable(2, srvHandle);
        m_cmdList->DrawInstanced(3, 1, 0, 0);

        CD3DX12_RESOURCE_BARRIER hdrToCommon = CD3DX12_RESOURCE_BARRIER::Transition(
            m_hdrRenderTarget.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COMMON);
        m_cmdList->ResourceBarrier(1, &hdrToCommon);

        CD3DX12_RESOURCE_BARRIER bloomToCommon = CD3DX12_RESOURCE_BARRIER::Transition(
            m_bloomTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COMMON);
        m_cmdList->ResourceBarrier(1, &bloomToCommon);

        CD3DX12_RESOURCE_BARRIER tempToCommon = CD3DX12_RESOURCE_BARRIER::Transition(
            m_tempBlurTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COMMON);
        m_cmdList->ResourceBarrier(1, &tempToCommon);

        OutputDebugStringA("[POST-PROCESS] Applied\n");
        return;
    }

    CD3DX12_RESOURCE_BARRIER lumToWrite = CD3DX12_RESOURCE_BARRIER::Transition(
        m_luminanceTexture.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_cmdList->ResourceBarrier(1, &lumToWrite);

    CD3DX12_CPU_DESCRIPTOR_HANDLE lumRTV(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_lumRTVIndex,
        m_rtvDescSize);
    m_cmdList->ClearRenderTargetView(lumRTV, clearColorLum, 0, nullptr);
    m_cmdList->OMSetRenderTargets(1, &lumRTV, FALSE, nullptr);
    m_cmdList->SetPipelineState(m_luminancePSO.Get());
    m_cmdList->SetGraphicsRootSignature(m_postProcessRootSig.Get());
    m_cmdList->SetGraphicsRootConstantBufferView(0, m_postProcessCB->GetGPUVirtualAddress());
    m_cmdList->SetGraphicsRootDescriptorTable(2, srvHandle);
    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmdList->DrawInstanced(3, 1, 0, 0);

    CD3DX12_RESOURCE_BARRIER lumToCommon = CD3DX12_RESOURCE_BARRIER::Transition(
        m_luminanceTexture.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COMMON);
    m_cmdList->ResourceBarrier(1, &lumToCommon);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_frameIndex,
        m_rtvDescSize);
    m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    if (m_motionBlurIntensity > 0.01f) {
        if (m_motionBlurPSO) {
            m_cmdList->SetPipelineState(m_motionBlurPSO.Get());
            m_cmdList->SetGraphicsRootConstantBufferView(1, m_motionBlurCB->GetGPUVirtualAddress());
        }
    }
    else {
        m_cmdList->SetPipelineState(m_toneMapPSO.Get());
    }

    m_cmdList->SetGraphicsRootSignature(m_postProcessRootSig.Get());
    m_cmdList->SetGraphicsRootConstantBufferView(0, m_postProcessCB->GetGPUVirtualAddress());
    m_cmdList->SetGraphicsRootDescriptorTable(2, srvHandle);
    m_cmdList->DrawInstanced(3, 1, 0, 0);

    CD3DX12_RESOURCE_BARRIER hdrToCommon = CD3DX12_RESOURCE_BARRIER::Transition(
        m_hdrRenderTarget.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON);
    m_cmdList->ResourceBarrier(1, &hdrToCommon);

    OutputDebugStringA("[POST-PROCESS] Tone mapping applied\n");
}