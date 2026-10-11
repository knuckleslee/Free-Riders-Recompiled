#include "menu_steady_hotkey.h"

#include "pose_stability.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace sfr {
namespace {
// The title is changed on a thread of its own: setting another thread's
// window text waits for that thread (the presentation's) to answer, and the
// input update must never wait on it -- the presentation may be waiting on
// the game. Each change gives up after a quarter of a second.
std::mutex title_lock;
std::condition_variable title_wake;
HWND title_window = nullptr;
bool title_wanted = false;
std::wstring original;
HWND original_of = nullptr;

void title_worker() {
    std::unique_lock guard(title_lock);
    for (;;) {
        title_wake.wait(guard, [] { return title_wanted; });
        title_wanted = false;
        HWND window = title_window;
        guard.unlock();
        if (window && IsWindow(window)) {
            DWORD_PTR result = 0;
            if (window != original_of) {
                wchar_t text[256]{};
                if (SendMessageTimeoutW(window, WM_GETTEXT, 256, reinterpret_cast<LPARAM>(text),
                                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &result)) {
                    original = text;
                    original_of = window;
                }
            }
            if (window == original_of) {
                const bool chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
                const bool on = menu_steadying();
                const std::wstring title = original + (chinese ? (on ? L"  [游標穩定：開（F8 切換）]" : L"  [游標穩定：關（F8 切換）]")
                                                               : (on ? L"  [Menu steadying: on (F8)]" : L"  [Menu steadying: off (F8)]"));
                SendMessageTimeoutW(window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(title.c_str()),
                                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &result);
            }
        }
        guard.lock();
    }
}

void show(HWND window) {
    static std::once_flag started;
    std::call_once(started, [] { std::thread(title_worker).detach(); });
    std::lock_guard guard(title_lock);
    title_window = window;
    title_wanted = true;
    title_wake.notify_one();
}

HWND game_window = nullptr;
bool held = false;
}

void poll_menu_steady_hotkey() {
    // Only the game's own window, in front: F8 elsewhere is somebody else's.
    HWND window = GetForegroundWindow();
    DWORD process = 0;
    if (!window || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId()) {
        held = false;
        return;
    }
    // The adjustment windows are this process's too; the title goes on the
    // game's, the first window of ours found in front.
    if (!game_window) {
        game_window = window;
        show(game_window);
    }
    const bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (down && !held) {
        set_menu_steadying(!menu_steadying());
        std::cerr << "NUI_MENU_STEADY on=" << menu_steadying() << '\n';
        show(IsWindow(game_window) ? game_window : window);
    }
    held = down;
}
}
#else
namespace sfr {
void poll_menu_steady_hotkey() {}
}
#endif
