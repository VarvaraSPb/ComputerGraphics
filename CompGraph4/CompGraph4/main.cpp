// main.cpp
#include "Window.h"
#include <iostream>

int main() {
    try {
        // Создаем окно
        Window window(L"My Game Window", 800, 600);

        // Включаем Raw Input (это нужно сделать один раз при старте)
        RAWINPUTDEVICE rid[1];
        rid[0].usUsagePage = 0x01; // Generic Desktop Controls
        rid[0].usUsage = 0x02;     // Mouse
        rid[0].dwFlags = 0;
        rid[0].hwndTarget = nullptr;

        if (!RegisterRawInputDevices(rid, 1, sizeof(RAWINPUTDEVICE))) {
            std::cerr << "Failed to register raw input device." << std::endl;
        }

        // Запускаем цикл
        int exitCode = window.Run();
        std::cout << "\nApplication exited with code: " << exitCode << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}