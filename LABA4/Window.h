#pragma once

#include <Windows.h>
#include <string>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>

using namespace DirectX;

class Window {
public:
    Window(const std::wstring& title, int width, int height);
    ~Window();

    int Run();
    HWND GetHwnd() const { return hwnd; }
    void RequestExit(int exitCode = 0);
    bool IsExitRequested() const { return isExitRequested; }

private:
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    ID3D11DepthStencilView* depthStencilView = nullptr;
    ID3D11Texture2D* depthStencilBuffer = nullptr;

    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* indexBuffer = nullptr;
    ID3D11Buffer* constantBuffer = nullptr;

    ID3D11RasterizerState* rasterizerState = nullptr;
    ID3D11DepthStencilState* depthStencilState = nullptr;
    ID3D11BlendState* blendState = nullptr;

    XMMATRIX worldMatrix;
    XMMATRIX viewMatrix;
    XMMATRIX projectionMatrix;

    XMFLOAT3 cameraPos = XMFLOAT3(0.0f, 2.0f, -5.0f);
    XMFLOAT3 cameraFront = XMFLOAT3(0.0f, 0.0f, 1.0f);
    XMFLOAT3 cameraUp = XMFLOAT3(0.0f, 1.0f, 0.0f);

    bool firstMouse = true;
    float lastX = 0.0f, lastY = 0.0f;
    bool mouseCaptured = false;
    static constexpr float mouseSensitivity = 0.002f;
    static constexpr float cameraSpeed = 1.5f;

    void InitializeDirectX();
    void CreateShaders();
    void CreateConstantBuffer();
    void CreatePSO();
    void CreateCubeGeometry();
    void UpdateMatrices();
    void RenderFrame();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static Window* currentInstance;

    HWND hwnd;
    std::wstring windowTitle;
    int windowWidth;
    int windowHeight;
    bool isExitRequested;
    int exitCode;

    void RegisterWindowClass();
    HWND CreateNativeWindow();
};