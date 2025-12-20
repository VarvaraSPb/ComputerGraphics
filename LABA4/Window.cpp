#include "Window.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <iostream>

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

Window* Window::currentInstance = nullptr;

constexpr float Window::mouseSensitivity;
constexpr float Window::cameraSpeed;

struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT3 normal;
    XMFLOAT3 color;
};

struct ConstantBuffer {
    XMMATRIX worldViewProjection;
    XMMATRIX world;
    XMFLOAT3 lightDir;
    float padding1;
    XMFLOAT3 cameraPos;
    float padding2;
};

Window::Window(const std::wstring& title, int width, int height)
    : windowTitle(title), windowWidth(width), windowHeight(height),
    isExitRequested(false), exitCode(0) {
    RegisterWindowClass();
    hwnd = CreateNativeWindow();
    if (!hwnd) {
        throw std::runtime_error("Failed to create window.");
    }
    currentInstance = this;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    InitializeDirectX();
}

Window::~Window() {
    if (vertexBuffer) vertexBuffer->Release();
    if (indexBuffer) indexBuffer->Release();
    if (constantBuffer) constantBuffer->Release();
    if (vertexShader) vertexShader->Release();
    if (pixelShader) pixelShader->Release();
    if (inputLayout) inputLayout->Release();
    if (rasterizerState) rasterizerState->Release();
    if (depthStencilState) depthStencilState->Release();
    if (blendState) blendState->Release();
    if (renderTargetView) renderTargetView->Release();
    if (depthStencilView) depthStencilView->Release();
    if (depthStencilBuffer) depthStencilBuffer->Release();
    if (swapChain) swapChain->Release();
    if (d3dContext) d3dContext->Release();
    if (d3dDevice) d3dDevice->Release();
    if (hwnd) DestroyWindow(hwnd);
}

void Window::RegisterWindowClass() {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"MyAppWindowClass";
    if (!RegisterClassExW(&wcex)) {
        throw std::runtime_error("Failed to register window class.");
    }
}

HWND Window::CreateNativeWindow() {
    RECT rect = { 0, 0, windowWidth, windowHeight };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hWnd = CreateWindowExW(
        0, L"MyAppWindowClass", windowTitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );
    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
    }
    return hWnd;
}

void Window::InitializeDirectX() {
    D3D_FEATURE_LEVEL featureLevel;
    UINT createDeviceFlags = 0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags, nullptr, 0, D3D11_SDK_VERSION,
        &d3dDevice, &featureLevel, &d3dContext);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION,
            &d3dDevice, &featureLevel, &d3dContext);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create D3D11 device.");
        }
    }

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = windowWidth;
    sd.BufferDesc.Height = windowHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGIDevice* dxgiDevice = nullptr;
    d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    IDXGIAdapter* dxgiAdapter = nullptr;
    dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);
    IDXGIFactory* dxgiFactory = nullptr;
    dxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&dxgiFactory);
    hr = dxgiFactory->CreateSwapChain(d3dDevice, &sd, &swapChain);
    dxgiDevice->Release(); dxgiAdapter->Release(); dxgiFactory->Release();
    if (FAILED(hr)) throw std::runtime_error("Failed to create swap chain.");

    ID3D11Texture2D* backBuffer = nullptr;
    hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(hr)) throw std::runtime_error("Failed to get back buffer.");
    hr = d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
    backBuffer->Release();
    if (FAILED(hr)) throw std::runtime_error("Failed to create RTV.");

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = windowWidth;
    depthDesc.Height = windowHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = d3dDevice->CreateTexture2D(&depthDesc, nullptr, &depthStencilBuffer);
    if (FAILED(hr)) throw std::runtime_error("Failed to create depth buffer.");
    hr = d3dDevice->CreateDepthStencilView(depthStencilBuffer, nullptr, &depthStencilView);
    if (FAILED(hr)) throw std::runtime_error("Failed to create DSV.");

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)windowWidth;
    vp.Height = (float)windowHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    d3dContext->RSSetViewports(1, &vp);

    worldMatrix = XMMatrixIdentity();
    viewMatrix = XMMatrixLookAtLH(
        XMLoadFloat3(&cameraPos),
        XMVectorAdd(XMLoadFloat3(&cameraPos), XMLoadFloat3(&cameraFront)),
        XMLoadFloat3(&cameraUp)
    );
    projectionMatrix = XMMatrixPerspectiveFovLH(
        XM_PIDIV4, (float)windowWidth / (float)windowHeight, 0.1f, 100.0f);

    CreateShaders();
    CreateConstantBuffer();
    CreatePSO();
    CreateCubeGeometry();
}

void Window::CreateShaders() {
    const char* vsCode = R"(
        cbuffer Constants : register(b0) {
            matrix WorldViewProjection;
            matrix World;
            float3 LightDir;
            float3 CameraPos;
        }
        struct VS_INPUT {
            float3 Pos : POSITION;
            float3 Normal : NORMAL;
            float3 Color : COLOR;
        };
        struct PS_INPUT {
            float4 Pos : SV_POSITION;
            float3 WorldPos : TEXCOORD0;
            float3 Normal : TEXCOORD1;
            float3 Color : TEXCOORD2;
        };
        PS_INPUT VS(VS_INPUT input) {
            PS_INPUT output;
            output.Pos = mul(float4(input.Pos, 1.0f), WorldViewProjection);
            output.WorldPos = mul(float4(input.Pos, 1.0f), World).xyz;
            output.Normal = mul(input.Normal, (float3x3)World);
            output.Color = input.Color;
            return output;
        }
    )";

    const char* psCode = R"(
        cbuffer Constants : register(b0) {
            matrix WorldViewProjection;
            matrix World;
            float3 LightDir;
            float3 CameraPos;
        }
        struct PS_INPUT {
            float4 Pos : SV_POSITION;
            float3 WorldPos : TEXCOORD0;
            float3 Normal : TEXCOORD1;
            float3 Color : TEXCOORD2;
        };
        float4 PS(PS_INPUT input) : SV_Target {
            float3 lightDir = normalize(LightDir); // ✅ без минуса!
            float3 viewDir = normalize(CameraPos - input.WorldPos);
            float3 normal = normalize(input.Normal);
            float diffuse = max(0.0f, dot(normal, lightDir));
            float specular = 0.0f;
            float shininess = 32.0f;
            if (diffuse > 0.0f) {
                float3 reflectDir = reflect(-lightDir, normal);
                specular = pow(max(0.0f, dot(viewDir, reflectDir)), shininess);
            }
            float4 ambient = float4(0.15f, 0.15f, 0.15f, 1.0f);
            float4 diffuseColor = float4(input.Color, 1.0f);
            float4 specularColor = float4(0.8f, 0.8f, 0.8f, 1.0f);
            return ambient + diffuse * diffuseColor + specular * specularColor;
        }
    )";

    ID3DBlob* vsBlob = nullptr, * psBlob = nullptr, * errorBlob = nullptr;
    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "VS error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
            errorBlob->Release();
        }
        throw std::runtime_error("Failed to compile vertex shader.");
    }

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "PS error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
            errorBlob->Release();
        }
        vsBlob->Release();
        throw std::runtime_error("Failed to compile pixel shader.");
    }

    hr = d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); throw std::runtime_error("Create VS failed."); }

    hr = d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); throw std::runtime_error("Create PS failed."); }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    hr = d3dDevice->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
    vsBlob->Release();
    psBlob->Release();
    if (FAILED(hr)) throw std::runtime_error("Create input layout failed.");
}

void Window::CreateConstantBuffer() {
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(ConstantBuffer);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = d3dDevice->CreateBuffer(&bd, nullptr, &constantBuffer);
    if (FAILED(hr)) throw std::runtime_error("Create constant buffer failed.");
}

void Window::CreatePSO() {
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    d3dDevice->CreateRasterizerState(&rsDesc, &rasterizerState);

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    d3dDevice->CreateDepthStencilState(&dsDesc, &depthStencilState);

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    d3dDevice->CreateBlendState(&blendDesc, &blendState);
}

void Window::CreateCubeGeometry() {
    Vertex vertices[] = {
        // Передняя грань
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(0.5f, -0.5f, 0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(0.5f, 0.5f, 0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(-0.5f, 0.5f, 0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        // Задняя грань 
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
        { XMFLOAT3(0.5f, 0.5f, -0.5f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
        { XMFLOAT3(-0.5f, 0.5f, -0.5f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
        // Левая 
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
        { XMFLOAT3(-0.5f, 0.5f, 0.5f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
        { XMFLOAT3(-0.5f, 0.5f, -0.5f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
        // Правая
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) },
        { XMFLOAT3(0.5f, -0.5f, 0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) },
        { XMFLOAT3(0.5f, 0.5f, 0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) },
        { XMFLOAT3(0.5f, 0.5f, -0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) },
        // Верх 
        { XMFLOAT3(-0.5f, 0.5f, -0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) },
        { XMFLOAT3(-0.5f, 0.5f, 0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) },
        { XMFLOAT3(0.5f, 0.5f, 0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) },
        { XMFLOAT3(0.5f, 0.5f, -0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) },
        // Низ 
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f, 0.5f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) },
    };

    WORD indices[] = {
        0,1,2, 0,2,3,     
        4,6,5, 4,7,6,  
        8,10,9, 8,11,10, 
        12,13,14, 12,14,15,
        16,17,18, 16,18,19, 
        20,22,21, 20,23,22 
    };

    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = sizeof(vertices);
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vData = { vertices };
    HRESULT hr = d3dDevice->CreateBuffer(&vbd, &vData, &vertexBuffer);
    if (FAILED(hr)) throw std::runtime_error("Create vertex buffer failed.");

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = sizeof(indices);
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iData = { indices };
    hr = d3dDevice->CreateBuffer(&ibd, &iData, &indexBuffer);
    if (FAILED(hr)) throw std::runtime_error("Create index buffer failed.");
}

void Window::UpdateMatrices() {
    XMVECTOR eye = XMLoadFloat3(&cameraPos);
    XMVECTOR at = XMVectorAdd(eye, XMLoadFloat3(&cameraFront));
    XMVECTOR up = XMLoadFloat3(&cameraUp);

    viewMatrix = XMMatrixLookAtLH(eye, at, up);

    projectionMatrix = XMMatrixPerspectiveFovLH(
        XM_PIDIV4,
        static_cast<float>(windowWidth) / static_cast<float>(windowHeight),
        0.1f,
        100.0f
    );

    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX wvp = world * viewMatrix * projectionMatrix;

    ConstantBuffer cb;
    cb.worldViewProjection = XMMatrixTranspose(wvp);
    cb.world = XMMatrixTranspose(world);
    cb.lightDir = XMFLOAT3(-0.7f, 1.0f, -0.5f); 
    cb.cameraPos = cameraPos;

    D3D11_MAPPED_SUBRESOURCE mapped;
    d3dContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &cb, sizeof(cb));
    d3dContext->Unmap(constantBuffer, 0);
}

void Window::RenderFrame() {
    const float deltaTime = 1.0f / 60.0f;
    const float velocity = cameraSpeed * deltaTime;

    XMFLOAT3 frontHoriz(cameraFront.x, 0.0f, cameraFront.z);
    XMVECTOR frontNorm = XMVector3Normalize(XMLoadFloat3(&frontHoriz));
    XMFLOAT3 front;
    XMStoreFloat3(&front, frontNorm);

    XMVECTOR rightVec = XMVector3Cross(XMLoadFloat3(&front), XMLoadFloat3(&cameraUp));
    XMFLOAT3 right;
    XMStoreFloat3(&right, rightVec);

    if (GetAsyncKeyState('W') & 0x8000) {
        cameraPos.x += front.x * velocity;
        cameraPos.z += front.z * velocity;
    }
    if (GetAsyncKeyState('S') & 0x8000) {
        cameraPos.x -= front.x * velocity;
        cameraPos.z -= front.z * velocity;
    }
    if (GetAsyncKeyState('A') & 0x8000) {
        cameraPos.x -= right.x * velocity;
        cameraPos.z -= right.z * velocity;
    }
    if (GetAsyncKeyState('D') & 0x8000) {
        cameraPos.x += right.x * velocity;
        cameraPos.z += right.z * velocity;
    }
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
        cameraPos.y += velocity;
    }
    if (GetAsyncKeyState(VK_LSHIFT) & 0x8000 || GetAsyncKeyState(VK_RSHIFT) & 0x8000) {
        cameraPos.y -= velocity;
    }

    static int frameCount = 0;
    if (++frameCount % 60 == 0) {
        std::cout << "Frame: " << frameCount << std::endl;
    }

    float clearColor[4] = { 0.0f, 0.1f, 0.2f, 1.0f };
    d3dContext->ClearRenderTargetView(renderTargetView, clearColor);
    d3dContext->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    d3dContext->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
    d3dContext->RSSetState(rasterizerState);
    d3dContext->OMSetDepthStencilState(depthStencilState, 0);
    d3dContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    d3dContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    d3dContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R16_UINT, 0);
    d3dContext->IASetInputLayout(inputLayout);
    d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    d3dContext->VSSetShader(vertexShader, nullptr, 0);
    d3dContext->PSSetShader(pixelShader, nullptr, 0);
    d3dContext->VSSetConstantBuffers(0, 1, &constantBuffer);
    d3dContext->PSSetConstantBuffers(0, 1, &constantBuffer);

    UpdateMatrices();
    d3dContext->DrawIndexed(36, 0, 0);
    swapChain->Present(1, 0);
}

int Window::Run() {
    MSG msg = {};
    while (!isExitRequested) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                RequestExit();
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) RequestExit();
        RenderFrame();
    }
    return exitCode;
}

void Window::RequestExit(int exitCode) {
    this->exitCode = exitCode;
    isExitRequested = true;
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    Window* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<Window*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else {
        pThis = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        switch (uMsg) {
        case WM_DESTROY:
        case WM_CLOSE:
            pThis->RequestExit();
            return 0;

        case WM_LBUTTONDOWN: {
            ShowCursor(FALSE);
            pThis->mouseCaptured = true;
            pThis->firstMouse = true; 

            RECT rect;
            GetClientRect(hwnd, &rect);
            POINT center = { rect.left + (rect.right - rect.left) / 2,
                             rect.top + (rect.bottom - rect.top) / 2 };
            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
            break;
        }

        case WM_LBUTTONUP: {
            ShowCursor(TRUE);
            pThis->mouseCaptured = false;
            break;
        }

        case WM_MOUSEMOVE: {
            if (pThis->mouseCaptured) {
                POINT currentPos;
                GetCursorPos(&currentPos);
                ScreenToClient(hwnd, &currentPos);

                RECT rect;
                GetClientRect(hwnd, &rect);
                float centerX = static_cast<float>(rect.left + (rect.right - rect.left) / 2);
                float centerY = static_cast<float>(rect.top + (rect.bottom - rect.top) / 2);

                float xoffset = static_cast<float>(currentPos.x) - centerX;
                float yoffset = static_cast<float>(currentPos.y) - centerY; 

                POINT center = { static_cast<LONG>(centerX), static_cast<LONG>(centerY) };
                ClientToScreen(hwnd, &center);
                SetCursorPos(center.x, center.y);

                xoffset *= pThis->mouseSensitivity;
                yoffset *= pThis->mouseSensitivity;

                float yaw = atan2f(pThis->cameraFront.z, pThis->cameraFront.x) + xoffset;
                float lenXZ = sqrtf(pThis->cameraFront.x * pThis->cameraFront.x + pThis->cameraFront.z * pThis->cameraFront.z);
                float pitch = atan2f(pThis->cameraFront.y, lenXZ) + yoffset;

                // Ограничение pitch
                if (pitch > XM_PIDIV2 - 0.01f) pitch = XM_PIDIV2 - 0.01f;
                if (pitch < -XM_PIDIV2 + 0.01f) pitch = -XM_PIDIV2 + 0.01f;

                pThis->cameraFront.x = cosf(pitch) * cosf(yaw);
                pThis->cameraFront.y = sinf(pitch);
                pThis->cameraFront.z = cosf(pitch) * sinf(yaw);
            }
            break;
        }

        default:
            break;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}