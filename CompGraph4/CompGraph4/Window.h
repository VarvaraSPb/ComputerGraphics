// Window.h
#pragma once

#include <Windows.h>
#include <string>

class Window {
public:
    // Конструктор и деструктор
    Window(const std::wstring& title, int width, int height);
    ~Window();

    // Запуск основного цикла приложения
    int Run();

    // Получение дескриптора окна
    HWND GetHwnd() const { return hwnd; }

    // Установка флага выхода из цикла
    void RequestExit(int exitCode = 0);

private:
    // Статическая функция-оконная процедура (обязательно static!)
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Указатель на текущий экземпляр окна (для передачи в WndProc)
    static Window* currentInstance;

    // Члены класса
    HWND hwnd;
    std::wstring windowTitle;
    int windowWidth;
    int windowHeight;
    bool isExitRequested;
    int exitCode;

    // Вспомогательные методы
    void RegisterWindowClass();
    HWND CreateNativeWindow();
};