#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;

class Gbuffer
{
public:
    static constexpr int COUNT = 4;

    bool Initialize(ID3D12Device* device, int width, int height);
    void Bind(ID3D12GraphicsCommandList* cmdList);
    void Unbind(ID3D12GraphicsCommandList* cmdList);
    void Clear(ID3D12GraphicsCommandList* cmdList, const float clearColor[4]);
    void TransitionToRead(ID3D12GraphicsCommandList* cmdList);
    void TransitionToWrite(ID3D12GraphicsCommandList* cmdList);


    ID3D12DescriptorHeap* GetSRVHeap() const { return m_srvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetAlbedoSRV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSRV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSpecularSRV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetPositionSRV() const;

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    ComPtr<ID3D12Resource> m_renderTargets[COUNT];
    ComPtr<ID3D12Resource> m_depthStencil;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    UINT m_rtvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;
    int m_width = 0;
    int m_height = 0;
};
