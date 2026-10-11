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

#include <iostream>
#include <string>

namespace sfr {
namespace {
HWND titled = nullptr;
std::wstring original;
bool shown = false, held = false;

void show(HWND window) {
    if (window != titled) {
        wchar_t text[256]{};
        GetWindowTextW(window, text, 256);
        titled = window;
        original = text;
    }
    const bool chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    const bool on = menu_steadying();
    const std::wstring state = chinese ? (on ? L"  [游標穩定：開（F8 切換）]" : L"  [游標穩定：關（F8 切換）]")
                                       : (on ? L"  [Menu steadying: on (F8)]" : L"  [Menu steadying: off (F8)]");
    SetWindowTextW(window, (original + state).c_str());
}
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
    if (!shown) {
        shown = true;
        show(window);
    }
    const bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (down && !held) {
        set_menu_steadying(!menu_steadying());
        std::cerr << "NUI_MENU_STEADY on=" << menu_steadying() << '\n';
        show(titled && IsWindow(titled) ? titled : window);
    }
    held = down;
}
}
#else
namespace sfr {
void poll_menu_steady_hotkey() {}
}
#endif
