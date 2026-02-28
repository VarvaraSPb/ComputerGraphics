#include "Window.h"
#include <d3dcompiler.h>
#include <iostream>
#include <typeinfo>

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
    XMFLOAT2 texCoord;
};

struct ConstantBuffer {
    XMMATRIX worldViewProjection;
    XMMATRIX world;
    XMFLOAT3 lightDir;
    float padding1;
    XMFLOAT3 cameraPos;
    float padding2;
    XMFLOAT2 texOffset;
    XMFLOAT2 texScale;
    float time;
    float padding3;
};

static_assert(sizeof(ConstantBuffer) % 16 == 0, "ConstantBuffer size must be 16-byte aligned");

// ==================== const/dest ====================

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
    if (sponzaVertexBuffer) sponzaVertexBuffer->Release();
    if (sponzaIndexBuffer) sponzaIndexBuffer->Release();
    if (constantBuffer) constantBuffer->Release();
    if (vertexShader) vertexShader->Release();
    if (pixelShader) pixelShader->Release();
    if (inputLayout) inputLayout->Release();
    if (rasterizerState) rasterizerState->Release();
    if (depthStencilState) depthStencilState->Release();
    if (blendState) blendState->Release();
    if (samplerState) samplerState->Release();
    if (renderTargetView) renderTargetView->Release();
    if (depthStencilView) depthStencilView->Release();
    if (depthStencilBuffer) depthStencilBuffer->Release();
    if (swapChain) swapChain->Release();
    if (d3dContext) d3dContext->Release();
    if (d3dDevice) d3dDevice->Release();
    if (hwnd) DestroyWindow(hwnd);

    for (auto* material : materials) {
        if (material) {
            delete material;
        }
    }
    materials.clear();
}

// ================== window =================-=

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

// ================ in. DIRECTX ===================

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

    cameraPos = XMFLOAT3(0.0f, 2.0f, -15.0f);
    cameraFront = XMFLOAT3(0.0f, 0.0f, 1.0f);
    cameraUp = XMFLOAT3(0.0f, 1.0f, 0.0f);

    worldMatrix = XMMatrixIdentity();
    viewMatrix = XMMatrixLookAtLH(
        XMLoadFloat3(&cameraPos),
        XMVectorAdd(XMLoadFloat3(&cameraPos), XMLoadFloat3(&cameraFront)),
        XMLoadFloat3(&cameraUp)
    );
    projectionMatrix = XMMatrixPerspectiveFovLH(
        XM_PIDIV4, (float)windowWidth / (float)windowHeight, 0.1f, 1000.0f);

    CreateShaders();
    CreateConstantBuffer();
    CreatePSO();
    CreateSamplerState();
    CreateSponzaGeometry();
}

// =================== shaders ===============

void Window::CreateShaders() {
    const char* vsCode = R"(
        cbuffer Constants : register(b0) {
            matrix WorldViewProjection;
            matrix World;
            float3 LightDir;
            float3 CameraPos;
            float2 TexOffset;
            float2 TexScale;
            float Time;
            float Padding;
        }
        
        struct VS_INPUT {
            float3 Pos : POSITION;
            float3 Normal : NORMAL;
            float2 TexCoord : TEXCOORD;
        };
        
        struct PS_INPUT {
            float4 Pos : SV_POSITION;
            float3 WorldPos : TEXCOORD0;
            float3 Normal : TEXCOORD1;
            float2 TexCoord : TEXCOORD2;
        };
        
        PS_INPUT VS(VS_INPUT input) {
            PS_INPUT output;
            output.Pos = mul(float4(input.Pos, 1.0f), WorldViewProjection);
            output.WorldPos = mul(float4(input.Pos, 1.0f), World).xyz;
            output.Normal = mul(input.Normal, (float3x3)World);
            output.TexCoord = input.TexCoord * TexScale + TexOffset;
            return output;
        }
    )";

    const char* psCode = R"(
        cbuffer Constants : register(b0) {
            matrix WorldViewProjection;
            matrix World;
            float3 LightDir;
            float3 CameraPos;
            float2 TexOffset;
            float2 TexScale;
            float Time;
            float Padding;
        }
        
        Texture2D DiffuseTexture : register(t0);
        SamplerState Sampler : register(s0);
        
        struct PS_INPUT {
            float4 Pos : SV_POSITION;
            float3 WorldPos : TEXCOORD0;
            float3 Normal : TEXCOORD1;
            float2 TexCoord : TEXCOORD2;
        };
        
        float4 PS(PS_INPUT input) : SV_Target {
            float4 diffuseColor = DiffuseTexture.Sample(Sampler, input.TexCoord);
            
            float3 normal = normalize(input.Normal);
            float3 lightDir = normalize(LightDir);
            float diffuse = max(0.2f, dot(normal, lightDir));
            
            return float4(diffuseColor.xyz * diffuse, 1.0f);
        }
    )";

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr,
        "VS", "vs_5_0", 0, 0, &vsBlob, &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Vertex shader compilation error:" << std::endl;
            std::cerr << (char*)errorBlob->GetBufferPointer() << std::endl;
            errorBlob->Release();
        }
        throw std::runtime_error("Failed to compile vertex shader.");
    }

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr,
        "PS", "ps_5_0", 0, 0, &psBlob, &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Pixel shader compilation error:" << std::endl;
            std::cerr << (char*)errorBlob->GetBufferPointer() << std::endl;
            errorBlob->Release();
        }
        vsBlob->Release();
        throw std::runtime_error("Failed to compile pixel shader.");
    }

    hr = d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(), nullptr, &vertexShader);
    if (FAILED(hr)) {
        vsBlob->Release();
        psBlob->Release();
        throw std::runtime_error("Failed to create vertex shader.");
    }

    hr = d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(), nullptr, &pixelShader);
    if (FAILED(hr)) {
        vsBlob->Release();
        psBlob->Release();
        throw std::runtime_error("Failed to create pixel shader.");
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    hr = d3dDevice->CreateInputLayout(layout, 3,
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);

    vsBlob->Release();
    psBlob->Release();

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create input layout.");
    }

    std::cout << "Shaders created successfully!" << std::endl;
}

void Window::CreateConstantBuffer() {
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(ConstantBuffer);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = d3dDevice->CreateBuffer(&bd, nullptr, &constantBuffer);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create constant buffer.");
    }

    std::cout << "Constant buffer created, size: " << sizeof(ConstantBuffer) << " bytes" << std::endl;
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

void Window::CreateSamplerState() {
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = d3dDevice->CreateSamplerState(&sampDesc, &samplerState);
    if (FAILED(hr)) {
        std::cerr << "Failed to create sampler state" << std::endl;
    }
    else {
        std::cout << "Sampler state created successfully" << std::endl;
    }
}

// ================= anim ====================

void Window::UpdateAnimation(float deltaTime) {
    totalTime += deltaTime;

    for (auto* material : materials) {
        if (material) {
            material->textureScaleU = 1.0f;
            material->textureScaleV = 1.0f;

            if (material->name.find("water") != std::string::npos ||
                material->name.find("Water") != std::string::npos ||
                material->name.find("curtain") != std::string::npos ||
                material->name.find("fabric") != std::string::npos) {

                material->animationTime += deltaTime;
                material->textureOffsetU = sin(material->animationTime * 0.5f) * 0.2f;
                material->textureOffsetV = cos(material->animationTime * 0.5f) * 0.2f;
                material->animationSpeed = 1.0f;
            }

            if (material->name.find("floor") != std::string::npos ||
                material->name.find("Floor") != std::string::npos ||
                material->name.find("wall") != std::string::npos ||
                material->name.find("Wall") != std::string::npos) {

                material->textureScaleU = 4.0f;  
                material->textureScaleV = 4.0f; 
            }
        }
    }
}

// ================== geom ================

void Window::CreateSponzaGeometry() {
    std::vector<OBJVertex> vertices;
    std::vector<UINT> indices;

    std::cout << "\n=== Starting Sponza geometry creation ===" << std::endl;

    char currentDir[256];
    GetCurrentDirectoryA(256, currentDir);
    std::cout << "Current directory: " << currentDir << std::endl;

    std::vector<std::string> possiblePaths = {
        "models/Sponza/sponza.obj",
        "models/Sponza/Sponza.obj",
        "../models/Sponza/sponza.obj",
        "C:/Users/Варя/source/repos/LABA4/LABA4/models/Sponza/sponza.obj"
    };

    bool loaded = false;
    for (const auto& path : possiblePaths) {
        std::cout << "\nTrying path: " << path << std::endl;

        DWORD fileAttr = GetFileAttributesA(path.c_str());
        if (fileAttr == INVALID_FILE_ATTRIBUTES) {
            std::cout << "  File does not exist" << std::endl;
            continue;
        }

        if (OBJLoader::LoadOBJ32(path, vertices, indices, materials, meshes, d3dDevice, true)) {
            loaded = true;
            std::cout << "  SUCCESS! Loaded Sponza from: " << path << std::endl;
            std::cout << "  Vertices: " << vertices.size() << ", Indices: " << indices.size() << std::endl;
            std::cout << "  Materials: " << materials.size() << ", Meshes: " << meshes.size() << std::endl;

            int texCount = 0;
            for (auto* mat : materials) {
                if (mat && mat->diffuseTexture) {
                    texCount++;
                }
            }
            std::cout << "  Materials with textures: " << texCount << " / " << materials.size() << std::endl;

            std::cout << "\n=== MATERIAL MAPPING ===" << std::endl;

            std::map<int, int> materialUsage;
            for (const auto& mesh : meshes) {
                materialUsage[mesh.materialIndex]++;
            }

            for (const auto& pair : materialUsage) {
                int matIndex = pair.first;
                int count = pair.second;

                if (matIndex >= 0 && matIndex < materials.size() && materials[matIndex]) {
                    std::cout << "Material " << matIndex << ": " << materials[matIndex]->name
                        << " used in " << count << " meshes" << std::endl;
                }
                else {
                    std::cout << "Material " << matIndex << ": INVALID used in " << count << " meshes" << std::endl;
                }
            }

            if (!meshes.empty()) {
                std::cout << "\nFirst mesh material index: " << meshes[0].materialIndex << std::endl;
                if (meshes[0].materialIndex >= 0 && meshes[0].materialIndex < materials.size()) {
                    std::cout << "First mesh material name: " << materials[meshes[0].materialIndex]->name << std::endl;
                }
            }

            std::cout << "=========================\n" << std::endl;

            break;
        }
    }

    if (!loaded) {
        std::cerr << "\nERROR: Failed to load Sponza!" << std::endl;
        return;
    }

    sponzaIndexCount = static_cast<UINT>(indices.size());

    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(OBJVertex));
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vData = {};
    vData.pSysMem = vertices.data();

    HRESULT hr = d3dDevice->CreateBuffer(&vbd, &vData, &sponzaVertexBuffer);
    if (FAILED(hr)) {
        std::cerr << "Failed to create vertex buffer" << std::endl;
        return;
    }

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(indices.size() * sizeof(UINT));
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA iData = {};
    iData.pSysMem = indices.data();

    hr = d3dDevice->CreateBuffer(&ibd, &iData, &sponzaIndexBuffer);
    if (FAILED(hr)) {
        std::cerr << "Failed to create index buffer" << std::endl;
        return;
    }

    std::cout << "=== Sponza geometry created successfully! ===\n" << std::endl;
}

// ================= matric/render ====================

void Window::UpdateMatrices() {
    XMVECTOR eye = XMLoadFloat3(&cameraPos);
    XMVECTOR at = XMVectorAdd(eye, XMLoadFloat3(&cameraFront));
    XMVECTOR up = XMLoadFloat3(&cameraUp);

    viewMatrix = XMMatrixLookAtLH(eye, at, up);
    projectionMatrix = XMMatrixPerspectiveFovLH(
        XM_PIDIV4,
        static_cast<float>(windowWidth) / static_cast<float>(windowHeight),
        0.1f,
        1000.0f
    );
}

void Window::RenderFrame() {
    const float deltaTime = 1.0f / 60.0f;
    const float velocity = cameraSpeed * deltaTime;

    UpdateAnimation(deltaTime);

    bool rightMousePressed = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

    int mouseX = InputDevice::GetMouseX();
    int mouseY = InputDevice::GetMouseY();

    static int lastMouseX = mouseX;
    static int lastMouseY = mouseY;
    static float yaw = 0.0f;
    static float pitch = 0.0f;

    if (rightMousePressed) {
        int deltaX = mouseX - lastMouseX;
        int deltaY = mouseY - lastMouseY;

        yaw += static_cast<float>(deltaX) * mouseSensitivity;
        pitch -= static_cast<float>(deltaY) * mouseSensitivity;

        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }

    lastMouseX = mouseX;
    lastMouseY = mouseY;

    XMFLOAT3 front;
    front.x = cos(XMConvertToRadians(yaw)) * cos(XMConvertToRadians(pitch));
    front.y = sin(XMConvertToRadians(pitch));
    front.z = sin(XMConvertToRadians(yaw)) * cos(XMConvertToRadians(pitch));

    XMVECTOR frontVec = XMVector3Normalize(XMLoadFloat3(&front));
    XMStoreFloat3(&cameraFront, frontVec);

    XMFLOAT3 frontHoriz(cameraFront.x, 0.0f, cameraFront.z);
    XMVECTOR frontHorizNorm = XMVector3Normalize(XMLoadFloat3(&frontHoriz));
    XMFLOAT3 moveFront;
    XMStoreFloat3(&moveFront, frontHorizNorm);

    XMVECTOR rightVec = XMVector3Cross(XMLoadFloat3(&cameraFront), XMLoadFloat3(&cameraUp));
    XMFLOAT3 right;
    XMStoreFloat3(&right, XMVector3Normalize(rightVec));

    if (GetAsyncKeyState('W') & 0x8000) {
        cameraPos.x += moveFront.x * velocity;
        cameraPos.z += moveFront.z * velocity;
    }
    if (GetAsyncKeyState('S') & 0x8000) {
        cameraPos.x -= moveFront.x * velocity;
        cameraPos.z -= moveFront.z * velocity;
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
    if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) {
        cameraPos.y -= velocity;
    }

    float clearColor[4] = { 0.0f, 0.1f, 0.2f, 1.0f };
    d3dContext->ClearRenderTargetView(renderTargetView, clearColor);
    d3dContext->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    d3dContext->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
    d3dContext->RSSetState(rasterizerState);
    d3dContext->OMSetDepthStencilState(depthStencilState, 0);
    d3dContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);

    d3dContext->IASetInputLayout(inputLayout);
    d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    d3dContext->VSSetShader(vertexShader, nullptr, 0);
    d3dContext->PSSetShader(pixelShader, nullptr, 0);
    d3dContext->VSSetConstantBuffers(0, 1, &constantBuffer);
    d3dContext->PSSetConstantBuffers(0, 1, &constantBuffer);
    d3dContext->PSSetSamplers(0, 1, &samplerState);

    UINT stride = sizeof(OBJVertex);
    UINT offset = 0;

    if (sponzaVertexBuffer != nullptr && sponzaIndexBuffer != nullptr && !meshes.empty()) {
        float scale = 0.02f;
        XMMATRIX world = XMMatrixScaling(scale, scale, scale) *
            XMMatrixRotationY(XMConvertToRadians(0.0f)) *
            XMMatrixTranslation(0.0f, -1.0f, 0.0f);

        UpdateMatrices();

        d3dContext->IASetVertexBuffers(0, 1, &sponzaVertexBuffer, &stride, &offset);
        d3dContext->IASetIndexBuffer(sponzaIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

        UINT currentIndexOffset = 0;
        for (size_t i = 0; i < meshes.size(); i++) {
            const auto& mesh = meshes[i];

            if (mesh.indices.empty()) continue;

            Material* material = nullptr;
            if (mesh.materialIndex >= 0 && mesh.materialIndex < materials.size()) {
                material = materials[mesh.materialIndex];
            }

            if (material && material->diffuseTexture) {
                d3dContext->PSSetShaderResources(0, 1, &material->diffuseTexture);
            }

            XMMATRIX wvp = world * viewMatrix * projectionMatrix;

            ConstantBuffer cb = {};
            cb.worldViewProjection = XMMatrixTranspose(wvp);
            cb.world = XMMatrixTranspose(world);
            cb.lightDir = XMFLOAT3(-0.7f, 1.0f, -0.5f);
            cb.cameraPos = cameraPos;

            if (material) {
                cb.texOffset = XMFLOAT2(material->textureOffsetU, material->textureOffsetV);
                cb.texScale = XMFLOAT2(material->textureScaleU, material->textureScaleV);
                cb.time = material->animationTime;
            }
            else {
                cb.texOffset = XMFLOAT2(0.0f, 0.0f);
                cb.texScale = XMFLOAT2(1.0f, 1.0f);
                cb.time = totalTime;
            }

            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(d3dContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                memcpy(mapped.pData, &cb, sizeof(cb));
                d3dContext->Unmap(constantBuffer, 0);
            }

            d3dContext->DrawIndexed(static_cast<UINT>(mesh.indices.size()), currentIndexOffset, 0);
            currentIndexOffset += static_cast<UINT>(mesh.indices.size());
        }

        ID3D11ShaderResourceView* nullSRV = nullptr;
        d3dContext->PSSetShaderResources(0, 1, &nullSRV);
    }

    swapChain->Present(1, 0);
}

void Window::Show() {
    ShowWindow(hwnd, SW_SHOW);
}

void Window::ProcessMessages() {
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT) {
            isExitRequested = true;
            exitCode = static_cast<int>(msg.wParam);
            break;
        }
    }
}

// ================ mes ===================

LRESULT Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (msg == WM_DESTROY) {
        if (window) {
            window->isExitRequested = true;
            window->exitCode = 0;
        }
        PostQuitMessage(0);
        return 0;
    }

    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        if (window) {
            window->isExitRequested = true;
            window->exitCode = 0;
        }
        PostQuitMessage(0);
        return 0;
    }

    if (msg == WM_INPUT) {
        InputDevice::ProcessRawInput(lParam);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}