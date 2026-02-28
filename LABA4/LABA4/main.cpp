#include "Window.h"
#include "InputDevice.h"
#include <iostream>

int main(int argc, char* argv[]) {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);

    std::cout << "=== 3D Renderer with Sponza ===" << std::endl;
    std::cout << "Starting application..." << std::endl;

    RAWINPUTDEVICE rid[2];

    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x06;
    rid[0].dwFlags = 0;
    rid[0].hwndTarget = 0;

    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x02;
    rid[1].dwFlags = 0;
    rid[1].hwndTarget = 0;

    if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
        std::cerr << "Failed to register raw input devices." << std::endl;
        return 1;
    }

    try {
        Window window(L"3D Renderer with Sponza", 1280, 720);
        std::cout << "Window created successfully." << std::endl;

        MSG msg = {};
        while (!window.IsExitRequested()) {
            window.ProcessMessages();
            window.RenderFrame();
        }

        std::cout << "Application exiting normally." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);

        if (f) fclose(f);
        FreeConsole();
        return 1;
    }

    if (f) fclose(f);
    FreeConsole();

    return 0;
}