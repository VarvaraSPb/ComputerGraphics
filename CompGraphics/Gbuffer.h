#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;

class Gbuffer
{
public:
    static constexpr int COUNT = 3;

    bool Initialize(ID3D12Device* device, ID3D12DescriptorHeap* sharedSrvHeap, int width, int height);

    void Bind(ID3D12GraphicsCommandList* cmdList);
    void Clear(ID3D12GraphicsCommandList* cmdList, const float clearColor[4]);
    void TransitionToRead(ID3D12GraphicsCommandList* cmdList);
    void TransitionToWrite(ID3D12GraphicsCommandList* cmdList);

    ID3D12DescriptorHeap* GetSRVHeap() const { return m_sharedSrvHeap; }

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    ComPtr<ID3D12Resource> m_renderTargets[COUNT];
    ComPtr<ID3D12Resource> m_depthStencil;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    ID3D12DescriptorHeap* m_sharedSrvHeap = nullptr;

    UINT m_rtvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;
    int m_width = 0;
    int m_height = 0;
};