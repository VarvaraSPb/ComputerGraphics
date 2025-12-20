#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include "Window.h"
#include <iostream>

int main() {
    MessageBox(nullptr, L"Программа запущена!", L"Отладка", MB_OK);

    try {
        Window window(L"My Cube", 800, 600);
        int exitCode = window.Run();
        std::cout << "\nApplication exited with code: " << exitCode << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}