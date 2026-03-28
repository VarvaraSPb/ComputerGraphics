#include "Gbuffer.h"

bool Gbuffer::Initialize(ID3D12Device* device, int width, int height)
{
    m_width = width;
    m_height = height;

    m_rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = COUNT;
    if (FAILED(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap))))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = COUNT;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap))))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_dsvHeap))))
        return false;

    DXGI_FORMAT formats[COUNT] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,       
        DXGI_FORMAT_R16G16B16A16_FLOAT,  
        DXGI_FORMAT_R32G32B32A32_FLOAT,  
    };

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < COUNT; i++)
    {
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = formats[i];
        texDesc.SampleDesc = { 1, 0 };
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

        if (FAILED(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            nullptr,
            IID_PPV_ARGS(&m_renderTargets[i])
        ))) return false;

        device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDescTex = {};
        srvDescTex.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDescTex.Format = formats[i];
        srvDescTex.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDescTex.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(m_renderTargets[i].Get(), &srvDescTex, srvHandle);

        rtvHandle.Offset(1, m_rtvDescriptorSize);
        srvHandle.Offset(1, m_srvDescriptorSize);
    }

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc = { 1, 0 };
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;

    if (FAILED(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClear,
        IID_PPV_ARGS(&m_depthStencil)
    ))) return false;

    device->CreateDepthStencilView(m_depthStencil.Get(), nullptr,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    return true;
}

void Gbuffer::Bind(ID3D12GraphicsCommandList* cmdList)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[COUNT];
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < COUNT; i++)
    {
        rtvHandles[i] = rtvHandle;
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmdList->OMSetRenderTargets(COUNT, rtvHandles, FALSE, &dsvHandle);
}

void Gbuffer::Unbind(ID3D12GraphicsCommandList* cmdList){}

void Gbuffer::Clear(ID3D12GraphicsCommandList* cmdList, const float clearColor[4])
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < COUNT; i++)
    {
        cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void Gbuffer::TransitionToRead(ID3D12GraphicsCommandList* cmdList)
{
    for (int i = 0; i < COUNT; i++)
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[i].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &barrier);
    }
}

void Gbuffer::TransitionToWrite(ID3D12GraphicsCommandList* cmdList)
{
    D3D12_RESOURCE_BARRIER barriers[COUNT];
    for (int i = 0; i < COUNT; i++)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[i].Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    cmdList->ResourceBarrier(COUNT, barriers);
}

D3D12_GPU_DESCRIPTOR_HANDLE Gbuffer::GetAlbedoSRV() const
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), 0, m_srvDescriptorSize);
}

D3D12_GPU_DESCRIPTOR_HANDLE Gbuffer::GetNormalSRV() const
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), 1, m_srvDescriptorSize);
}

D3D12_GPU_DESCRIPTOR_HANDLE Gbuffer::GetPositionSRV() const
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), 2, m_srvDescriptorSize);
}