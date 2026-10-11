#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>

namespace sfr {
// The game's own window in this process (native_presentation.cpp's class),
// or null: the launcher's preview has none.
inline HWND find_game_window() {
    for (HWND window = nullptr; (window = FindWindowExW(nullptr, window, L"SonicFreeRidersNativePresentation", nullptr));) {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process == GetCurrentProcessId() && IsWindowVisible(window) && !IsIconic(window)) return window;
    }
    return nullptr;
}

// Moves a window that is about to be shown to the middle of the game's
// window (kept on the desktop, should it be larger than the game's window).
// Without a game window it stays where Windows put it.
inline void center_over_game(HWND window) {
    const HWND game = find_game_window();
    RECT over{}, own{};
    if (!game || !window || !GetWindowRect(game, &over) || !GetWindowRect(window, &own)) return;
    const LONG width = own.right - own.left, height = own.bottom - own.top;
    LONG x = (over.left + over.right) / 2 - width / 2, y = (over.top + over.bottom) / 2 - height / 2;
    const LONG left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    x = (std::max)(left, (std::min)(x, left + GetSystemMetrics(SM_CXVIRTUALSCREEN) - width));
    y = (std::max)(top, (std::min)(y, top + GetSystemMetrics(SM_CYVIRTUALSCREEN) - height));
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
}
}
#endif
