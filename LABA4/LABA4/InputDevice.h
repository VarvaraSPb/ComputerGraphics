#pragma once

#include <Windows.h>

class InputDevice {
public:
    static void ProcessRawInput(LPARAM lParam);
    static bool IsKeyDown(int vkCode);
    static int GetMouseX();
    static int GetMouseY();
};
