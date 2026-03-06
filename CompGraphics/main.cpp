#include "Window.h"
#include "RenderingSystem.h"
#include "Timer.h"
#include "InputDevice.h"


class App
{
public:
    bool Init(HINSTANCE hInstance)
    {
        if (!m_window.Init(hInstance, 1280, 720, L"DX12 3D Sponza"))
            return false;

        m_window.SetResizeCallback([this](int w, int h) {
            m_renderingSystem.OnResize(w, h);
            });

        if (!m_renderingSystem.Init(m_window.GetHWND(),
            m_window.GetWidth(),
            m_window.GetHeight()))
            return false;

        m_renderingSystem.SetTexTiling(2.0f, 2.0f);
        m_renderingSystem.SetTexScroll(0.05f, 0.0f);
        m_renderingSystem.LoadObj("sponza.obj");

        AddTestLights();

        m_timer.Reset();
        return true;
    }

    void AddTestLights()
    {
    }

    void Show(int nCmdShow) { m_window.Show(nCmdShow); }

    int Run()
    {
        MSG msg{};
        while (true)
        {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                    return (int)msg.wParam;

                switch (msg.message)
                {
                case WM_KEYDOWN: m_input.OnKeyDown(msg.wParam); break;
                case WM_KEYUP: m_input.OnKeyUp(msg.wParam); break;
                case WM_MOUSEMOVE: m_input.OnMouseMove(LOWORD(msg.lParam), HIWORD(msg.lParam)); break;
                case WM_LBUTTONDOWN: m_input.OnMouseDown(0); break;
                case WM_LBUTTONUP: m_input.OnMouseUp(0); break;
                case WM_RBUTTONDOWN: m_input.OnMouseDown(1); break;
                case WM_RBUTTONUP: m_input.OnMouseUp(1); break;
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            m_timer.Tick();
            m_renderingSystem.UpdateCamera(m_timer.DeltaTime(), m_input);

            const float clear[] = { 0.1f, 0.1f, 0.15f, 1.0f };
            m_renderingSystem.BeginFrame(clear);
            m_renderingSystem.DrawScene(m_timer.TotalTime(), m_timer.DeltaTime());
            m_renderingSystem.EndFrame();

            m_input.EndFrame();
        }
    }

private:
    Window m_window;
    RenderingSystem m_renderingSystem;
    Timer m_timer;
    InputDevice m_input;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    App app;
    if (!app.Init(hInstance))
    {
        MessageBox(nullptr, L"Init failed!", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }
    app.Show(nCmdShow);
    return app.Run();
}