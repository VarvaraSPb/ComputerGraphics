// InputDevice.h
#pragma once

#include <Windows.h>

class InputDevice {
public:
    // Статический метод для обработки RAW INPUT
    static void ProcessRawInput(LPARAM lParam);

    // Методы для получения состояния клавиш/мыши (можно расширять)
    static bool IsKeyDown(int vkCode);
    static int GetMouseX();
    static int GetMouseY();
};