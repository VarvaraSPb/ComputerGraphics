#include "InputDevice.h"
#include <iostream>
#include <vector>

static std::vector<bool> keyState(256, false);
static int mouseX = 0, mouseY = 0;

void InputDevice::ProcessRawInput(LPARAM lParam) {
    UINT dwSize = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));

    if (dwSize == 0) {
        return;
    }

    BYTE* lpBuffer = new BYTE[dwSize];
    if (lpBuffer == nullptr) {
        return;
    }

    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, lpBuffer, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
        delete[] lpBuffer;
        return;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpBuffer);

    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        WORD vkCode = raw->data.keyboard.VKey;
        bool isKeyDown = (raw->data.keyboard.Flags & RI_KEY_BREAK) == 0;

        if (vkCode < 256) {
            keyState[vkCode] = isKeyDown;
        }
    }
    else if (raw->header.dwType == RIM_TYPEMOUSE) {
        int deltaX = raw->data.mouse.lLastX;
        int deltaY = raw->data.mouse.lLastY;

        mouseX += deltaX;
        mouseY += deltaY;
    }

    delete[] lpBuffer;
}

bool InputDevice::IsKeyDown(int vkCode) {
    if (vkCode >= 0 && vkCode < 256) {
        return keyState[vkCode];
    }
    return false;
}

int InputDevice::GetMouseX() {
    return mouseX;
}

int InputDevice::GetMouseY() {
    return mouseY;
}
