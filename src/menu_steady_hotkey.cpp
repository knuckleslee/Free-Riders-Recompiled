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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace sfr {
// Frames presented so far (guest_graphics_hooks.cpp).
extern std::atomic<uint32_t> present_count;

namespace {
// The title is changed on a thread of its own: setting another thread's
// window text waits for that thread (the presentation's) to answer, and the
// input update must never wait on it -- the presentation may be waiting on
// the game. Each change gives up after a quarter of a second.
//
// It says the frame rate, counted over the last half second, and, when a
// body drives the game, whether the menu steadying is on.
std::mutex title_lock;
std::condition_variable title_wake;
HWND title_window = nullptr;
bool title_wanted = false;

bool body_driven() {
    const char* const camera = std::getenv("SFR_CAMERA");
    return camera && (std::strcmp(camera, "motion") == 0 || std::strcmp(camera, "kinect") == 0);
}

void title_worker() {
    using clock = std::chrono::steady_clock;
    const bool chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    const bool body = body_driven();
    std::wstring original;
    HWND original_of = nullptr;
    uint32_t counted = present_count.load(std::memory_order_relaxed);
    clock::time_point since = clock::now();
    double fps = 0;
    std::unique_lock guard(title_lock);
    for (;;) {
        title_wake.wait_for(guard, std::chrono::milliseconds(500), [] { return title_wanted; });
        title_wanted = false;
        HWND window = title_window;
        guard.unlock();
        const clock::time_point now = clock::now();
        const double seconds = std::chrono::duration<double>(now - since).count();
        if (seconds >= 0.45) {
            const uint32_t presents = present_count.load(std::memory_order_relaxed);
            fps = (presents - counted) / seconds;
            counted = presents;
            since = now;
        }
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
                wchar_t rate[32];
                swprintf(rate, 32, L"  [FPS %.1f]", fps);
                std::wstring title = original + rate;
                if (body) {
                    const bool on = menu_steadying();
                    title += chinese ? (on ? L"  [游標穩定：開（F8 切換）]" : L"  [游標穩定：關（F8 切換）]")
                                     : (on ? L"  [Menu steadying: on (F8)]" : L"  [Menu steadying: off (F8)]");
                }
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
