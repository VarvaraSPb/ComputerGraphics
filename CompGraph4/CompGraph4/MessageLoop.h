#pragma once
#include "Window.h"

class MessageLoop
{
public:
    static int Run(Window& window)
    {
        MSG msg = {};

        while (!window.IsExitRequested())
        {
            // Обработка всех сообщений в очереди
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    window.RequestExit();
                    break;
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            // Здесь выполняется игровая логика
            // DoGameUpdate();
            // DoGameRender();

            // Небольшая пауза для снижения нагрузки на CPU
            Sleep(1);
        }

        return static_cast<int>(msg.wParam);
    }

    static int RunWithFixedUpdate(Window& window,
        std::function<void()> updateFunc,
        std::function<void()> renderFunc)
    {
        MSG msg = {};

        // Для фиксированного обновления
        constexpr float TARGET_FPS = 60.0f;
        constexpr float FRAME_TIME = 1000.0f / TARGET_FPS;

        auto lastTime = std::chrono::high_resolution_clock::now();
        float accumulatedTime = 0.0f;

        while (!window.IsExitRequested())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float, std::milli>(currentTime - lastTime).count();
            lastTime = currentTime;
            accumulatedTime += deltaTime;

            // Обработка сообщений
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    window.RequestExit();
                    break;
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            // Фиксированное обновление
            while (accumulatedTime >= FRAME_TIME)
            {
                if (updateFunc)
                    updateFunc();

                accumulatedTime -= FRAME_TIME;
            }

            // Рендеринг
            if (renderFunc)
                renderFunc();

            // Поддержание FPS
            if (deltaTime < FRAME_TIME)
            {
                Sleep(static_cast<DWORD>(FRAME_TIME - deltaTime));
            }
        }

        return static_cast<int>(msg.wParam);
    }
};