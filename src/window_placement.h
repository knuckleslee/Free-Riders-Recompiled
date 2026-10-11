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
// window, kept inside the monitor that middle is on (a window centred over
// three monitors would otherwise sit across their edges). Without a game
// window it stays where Windows put it.
inline void center_over_game(HWND window) {
    const HWND game = find_game_window();
    RECT over{}, own{};
    if (!game || !window || !GetWindowRect(game, &over) || !GetWindowRect(window, &own)) return;
    const POINT middle{(over.left + over.right) / 2, (over.top + over.bottom) / 2};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromPoint(middle, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    const RECT area = monitor.rcWork;
    const LONG width = (std::min)(own.right - own.left, area.right - area.left);
    const LONG height = (std::min)(own.bottom - own.top, area.bottom - area.top);
    const LONG x = std::clamp(middle.x - width / 2, area.left, area.right - width);
    const LONG y = std::clamp(middle.y - height / 2, area.top, area.bottom - height);
    SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}
}
#endif
