#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include "InputDevice.h"
#include "OBJLoader.h"

class Window {
public:
    int GetWidth() const { return windowWidth; }
    int GetHeight() const { return windowHeight; }

    Window(const std::wstring& title, int width, int height);
    ~Window();

    bool IsExitRequested() const { return isExitRequested; }
    int GetExitCode() const { return exitCode; }
    void Show();
    void ProcessMessages();
    void RenderFrame();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    static Window* currentInstance;

    std::wstring windowTitle;
    int windowWidth, windowHeight;
    HWND hwnd;
    bool isExitRequested;
    int exitCode;

    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    ID3D11Texture2D* depthStencilBuffer = nullptr;
    ID3D11DepthStencilView* depthStencilView = nullptr;

    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* constantBuffer = nullptr;

    ID3D11RasterizerState* rasterizerState = nullptr;
    ID3D11DepthStencilState* depthStencilState = nullptr;
    ID3D11BlendState* blendState = nullptr;

    ID3D11Buffer* cubeVertexBuffer = nullptr;
    ID3D11Buffer* cubeIndexBuffer = nullptr;

    ID3D11Buffer* sponzaVertexBuffer = nullptr;
    ID3D11Buffer* sponzaIndexBuffer = nullptr;
    UINT sponzaIndexCount = 0;  

    XMMATRIX worldMatrix;
    XMMATRIX viewMatrix;
    XMMATRIX projectionMatrix;

    XMFLOAT3 cameraPos = XMFLOAT3(0.0f, 2.0f, -15.0f); 
    XMFLOAT3 cameraFront = XMFLOAT3(0.0f, 0.0f, 1.0f);
    XMFLOAT3 cameraUp = XMFLOAT3(0.0f, 1.0f, 0.0f);

    static constexpr float cameraSpeed = 5.0f;
    static constexpr float mouseSensitivity = 0.1f;

    std::vector<Material*> materials;
    std::vector<OBJMesh> meshes;
    ID3D11SamplerState* samplerState = nullptr;

    float totalTime = 0.0f;

    void CreateSamplerState();
    void UpdateAnimation(float deltaTime);

    float yaw = 0.0f;
    float pitch = 0.0f;
    bool firstMouse = true;
    int lastMouseX = 0;
    int lastMouseY = 0;
    bool mouseCentered = false;

    void RegisterWindowClass();
    HWND CreateNativeWindow();
    void InitializeDirectX();
    void CreateShaders();
    void CreateConstantBuffer();
    void CreatePSO();
    //void CreateCubeGeometry();
    void CreateSponzaGeometry();  
    //void CreateFallbackSphere(std::vector<OBJVertex>& vertices, std::vector<UINT>& indices);
    void UpdateMatrices();
};