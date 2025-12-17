#include "Window.h"
#include "InputDevice.h" 
#include <iostream>
Window* Window::currentInstance = nullptr;

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
}

Window::~Window() {
    if (hwnd) {
        DestroyWindow(hwnd);
    }
}

void Window::RegisterWindowClass() {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"MyAppWindowClass";
    wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wcex)) {
        throw std::runtime_error("Failed to register window class.");
    }
}

HWND Window::CreateNativeWindow() {
    RECT rect = { 0, 0, windowWidth, windowHeight };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindowExW(
        0,
        L"MyAppWindowClass",
        windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
    }

    return hWnd;
}

int Window::Run() {
    MSG msg = {};
    while (!isExitRequested) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // --- МЕСТО ДЛЯ ИГРОВОЙ ЛОГИКИ ---
        // Например: обновление, рендеринг
        // std::cout << "."; // Пример: вывод точки в консоль
        // Sleep(16); // Приблизительно 60 FPS
        // -------------------------------
    }

    return exitCode;
}

void Window::RequestExit(int exitCode) {
    this->exitCode = exitCode;
    isExitRequested = true;
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Получаем указатель на экземпляр класса Window
    Window* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        // При создании окна получаем указатель из CREATESTRUCT
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<Window*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else {
        pThis = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        // Обрабатываем сообщения
        switch (uMsg) {
        case WM_DESTROY:
        case WM_CLOSE:
            pThis->RequestExit();
            return 0;

        case WM_INPUT:
            // Передаем обработку в InputDevice (рефакторинг)
            InputDevice::ProcessRawInput(lParam);
            break;

        default:
            break;
        }
    }

    // Если не обработано — вызываем стандартную процедуру
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}