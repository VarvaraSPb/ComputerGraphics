#include "RenderingSystem.h"
#include <stdexcept>
#include <cmath>
#include "InputDevice.h"

static void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("DirectX call failed");
}

RenderingSystem::~RenderingSystem() {
    if (m_initialized) FlushCommandQueue();
    if (m_constantBuffer && m_cbMapped) m_constantBuffer->Unmap(0, nullptr);
    if (m_pointLightBuffer && m_pointLightsMapped) m_pointLightBuffer->Unmap(0, nullptr);
    if (m_lightBuffer && m_lightMappedData) m_lightBuffer->Unmap(0, nullptr);
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
    CoUninitialize();
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
        CreateLightingResources();

        CreateRainLightBuffer();
        CreateRainLightSRV();

        CreateScreenQuad();

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
    rtvD.NumDescriptors = FRAME_COUNT;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvD, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvD{};
    dsvD.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvD.NumDescriptors = 1;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvD, IID_PPV_ARGS(&m_dsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC cbvD{};
    cbvD.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvD.NumDescriptors = 100 + (MAX_TEXTURES * 3);
    cbvD.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvD, IID_PPV_ARGS(&m_cbvSrvHeap)));
    m_cbvSrvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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

    CD3DX12_ROOT_PARAMETER params[2];
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }
    ThrowIfFailed(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
}

void RenderingSystem::CreateLightingRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
    CD3DX12_ROOT_PARAMETER params[2] = {};
    params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
    CD3DX12_STATIC_SAMPLER_DESC sampler(0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0, 0, D3D12_COMPARISON_FUNC_ALWAYS, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        0.0f, D3D12_SHADER_VISIBILITY_PIXEL);
    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr)) { if (errors) OutputDebugStringA((char*)errors->GetBufferPointer()); ThrowIfFailed(hr); }
    ThrowIfFailed(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_lightingRootSignature)));
}

void RenderingSystem::CreatePipelineStateObject() {
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
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA; blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD; blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO; blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.BlendState = blendDesc;
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1; pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
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
    psoDesc.RasterizerState.AntialiasedLineEnable = TRUE;
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
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE; blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO; blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1; psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
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
    UINT totalSize = m_cbSlotSize * MAX_SUBSETS * FRAME_COUNT;
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

    std::string dir;
    size_t p = path.find_last_of("/\\");
    if (p != std::string::npos) dir = path.substr(0, p + 1);

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

    // Diffuse
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

    // Normal map
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

    //Displacement map
    {
        std::wstring dispPath = L"textures/broken_stump/Broken_Stump_rkswd_High_4K_Displacement.jpg";
        TextureLoader::TextureData td;
        if (TextureLoader::LoadFromFile(dispPath, td) &&
            TextureLoader::CreateTexture(m_device.Get(), m_cmdList.Get(), td, mat.displacementTexture, mat.displacementUpload)) {
            srvDesc.Format = td.format;
            m_device->CreateShaderResourceView(mat.displacementTexture.Get(), &srvDesc, srvHandle);
        }
        else {
            m_device->CreateShaderResourceView(m_defaultDisplacementTex.Get(), &srvDesc, srvHandle);
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
    m_lightMappedData->DirLightDir = XMFLOAT4(0.0f, 1.0f, 0.0f, 0.0f);
    m_lightMappedData->DirLightColor = XMFLOAT4(1.0f, 1.0f, 0.7f, 2.0f);
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
    if (m_useDeferredRendering) {
        AddLight();
        RenderGeometryPass(totalTime);
        m_gbuffer.TransitionToRead(m_cmdList.Get());
        UpdateRainLights(deltaTime);
        RenderLightingPass();
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
            cb.DisplacementScale = 5.0f;  

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
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescSize);
    m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_cmdList->SetPipelineState(m_lightingPassPSO.Get());
    m_cmdList->SetGraphicsRootSignature(m_lightingRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 0, m_cbvSrvDescSize);
    m_cmdList->SetGraphicsRootDescriptorTable(1, srvHandle);
    if (m_lightBuffer) m_cmdList->SetGraphicsRootConstantBufferView(0, m_lightBuffer->GetGPUVirtualAddress());
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
        }
    }
    else {
        m_tKeyPressed = false;
    }

    float moveSpeed = m_cameraSpeed * deltaTime;
    XMFLOAT3 moveDelta = { 0, 0, 0 };
    if (input.IsKeyDown('W')) moveDelta.z += moveSpeed; if (input.IsKeyDown('S')) moveDelta.z -= moveSpeed;
    if (input.IsKeyDown('A')) moveDelta.x -= moveSpeed; if (input.IsKeyDown('D')) moveDelta.x += moveSpeed;
    if (input.IsKeyDown('Q')) moveDelta.y -= moveSpeed; if (input.IsKeyDown('E')) moveDelta.y += moveSpeed;
    if (input.MouseDX() != 0 || input.MouseDY() != 0) {
        float mouseSensitivity = 0.005f;
        m_cameraYaw += input.MouseDX() * mouseSensitivity; m_cameraPitch += input.MouseDY() * mouseSensitivity;
        if (m_cameraPitch < -XM_PIDIV2 + 0.1f) m_cameraPitch = -XM_PIDIV2 + 0.1f;
        if (m_cameraPitch > XM_PIDIV2 - 0.1f) m_cameraPitch = XM_PIDIV2 - 0.1f;
    }
    float rotateSpeed = 1.0f * deltaTime;
    if (input.IsKeyDown(VK_LEFT)) m_cameraYaw += rotateSpeed; if (input.IsKeyDown(VK_RIGHT)) m_cameraYaw -= rotateSpeed;
    if (input.IsKeyDown(VK_UP)) { m_cameraPitch += rotateSpeed; if (m_cameraPitch > XM_PIDIV2 - 0.1f) m_cameraPitch = XM_PIDIV2 - 0.1f; }
    if (input.IsKeyDown(VK_DOWN)) { m_cameraPitch -= rotateSpeed; if (m_cameraPitch < -XM_PIDIV2 + 0.1f) m_cameraPitch = -XM_PIDIV2 + 0.1f; }
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(m_cameraPitch, m_cameraYaw, 0);
    XMVECTOR moveVector = XMLoadFloat3(&moveDelta); moveVector = XMVector3TransformNormal(moveVector, rotationMatrix);
    XMVECTOR eyePos = XMLoadFloat3(&m_eye); eyePos = eyePos + moveVector; XMStoreFloat3(&m_eye, eyePos);
    XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
    forward = XMVector3TransformNormal(forward, rotationMatrix);
    XMVECTOR targetPos = eyePos + forward; XMStoreFloat3(&m_target, targetPos);
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