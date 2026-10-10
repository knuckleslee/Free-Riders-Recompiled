#include "camera_tuner.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace sfr {
namespace {
constexpr COLORREF background = RGB(19, 24, 33), foreground = RGB(221, 228, 238), dim = RGB(150, 164, 182);
constexpr COLORREF good = RGB(128, 225, 167), bad = RGB(255, 150, 130), warn = RGB(255, 205, 119);
constexpr COLORREF body_colour = RGB(70, 207, 236);
// The pose model's bones that are drawn: arms, shoulders, trunk and legs.
constexpr std::array<std::array<uint32_t, 2>, 12> bones{{
    {pose_point::shoulder_left, pose_point::shoulder_right}, {pose_point::hip_left, pose_point::hip_right},
    {pose_point::shoulder_left, pose_point::hip_left}, {pose_point::shoulder_right, pose_point::hip_right},
    {pose_point::shoulder_left, pose_point::elbow_left}, {pose_point::elbow_left, pose_point::wrist_left},
    {pose_point::shoulder_right, pose_point::elbow_right}, {pose_point::elbow_right, pose_point::wrist_right},
    {pose_point::hip_left, pose_point::knee_left}, {pose_point::knee_left, pose_point::ankle_left},
    {pose_point::hip_right, pose_point::knee_right}, {pose_point::knee_right, pose_point::ankle_right},
}};

void text(HDC dc, int x, int y, const std::wstring& words, COLORREF colour = foreground) {
    SetTextColor(dc, colour);
    TextOutW(dc, x, y, words.c_str(), int(words.size()));
}
}

struct CameraTunerWindow::Impl {
    CameraPlayer* player = nullptr;  // the game's, borrowed
    bool chinese = false;
    std::atomic<bool> is_closed{false};
    std::stop_token stopping;
    CameraView view;
    std::chrono::steady_clock::time_point opened_at{}, last_picture{};
    // Last member: joins before everything above is destroyed.
    std::jthread worker;

    std::wstring say(const wchar_t* english, const wchar_t* chinese_words) const {
        return chinese ? chinese_words : english;
    }

    void paint(HDC target, const RECT& rect) {
        const int width = rect.right - rect.left, height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0) return;
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
        if (!dc || !bitmap) { if (dc) DeleteDC(dc); if (bitmap) DeleteObject(bitmap); return; }
        const auto old_bitmap = SelectObject(dc, bitmap);
        HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                 chinese ? L"Microsoft JhengHei UI" : L"Segoe UI");
        const auto old_font = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
        HBRUSH brush = CreateSolidBrush(background);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT);

        const auto now = std::chrono::steady_clock::now();
        const auto seconds = [&](std::chrono::steady_clock::time_point from) {
            return std::chrono::duration<double>(now - from).count();
        };
        const CameraFraming framing = view.found ? camera_framing(view.body, view.picture.width, view.picture.height)
                                                 : CameraFraming{};
        CameraReadinessInput input;
        input.opened = true;
        input.seconds_open = seconds(opened_at);
        input.seconds_since_picture = view.picture.number ? seconds(last_picture) : -1;
        input.motion = view.picture.number ? view.motion : true;
        input.found = view.found;
        input.whole_body = framing.whole_body;
        const CameraReadiness readiness = camera_readiness(input);

        // Each step with its state, then what to do about the first that failed.
        const wchar_t* const names[] = {L"Camera", L"攝影機", L"Pictures", L"畫面", L"Body", L"人體偵測",
                                        L"Whole body", L"全身入鏡"};
        int x = 20;
        for (size_t step = 0; step < size_t(CameraStep::count); ++step) {
            const CameraStepState state = readiness.steps[step];
            const wchar_t* const mark = state == CameraStepState::passed ? L"✓ " :
                                        state == CameraStepState::failed ? L"✗ " : L"… ";
            const std::wstring item = mark + say(names[2 * step], names[2 * step + 1]);
            text(dc, x, 16, item, state == CameraStepState::passed ? good : state == CameraStepState::failed ? bad : dim);
            SIZE size{};
            GetTextExtentPoint32W(dc, item.c_str(), int(item.size()), &size);
            x += size.cx + 24;
        }
        std::wstring hint;
        switch (readiness.first_failed) {
        case CameraStep::camera:
        case CameraStep::pictures:
            hint = say(L"The webcam sends no pictures: close other programs using it, or plug it in again.",
                       L"Webcam 沒有傳來畫面：請關閉其他正在使用攝影機的程式，或重新插拔攝影機。");
            break;
        case CameraStep::body:
            hint = !view.motion ? say(L"Motion is off (camera picture only): choose motion in the launcher to play with your body.",
                                      L"目前只開啟攝影機畫面，沒有動作偵測：請在啟動器選擇體感動作。")
                                : say(L"Nobody found: face the camera, light the room evenly, and keep the light behind the camera, not behind you.",
                                      L"沒有偵測到人：請面向攝影機，讓房間光線均勻，光源在攝影機這一側、不要在你背後。");
            break;
        case CameraStep::whole_body:
            hint = framing.advice == CameraAdvice::to_the_middle
                       ? say(L"Part of you is off the side: move towards the middle of the picture.",
                             L"身體有一部分在畫面外：請往畫面中間移動。")
                       : say(L"Your whole body is not in view: step back, or place the camera lower and aim it level.",
                             L"全身沒有入鏡：請往後站，或把攝影機放低、水平朝向你。");
            break;
        case CameraStep::count:
            hint = readiness.ready() ? say(L"Ready: your whole body is in view.", L"準備完成：全身都在畫面內。")
                                     : say(L"Waiting for the webcam's first picture...", L"正在等待攝影機的第一個畫面…");
            break;
        }
        text(dc, 20, 40, hint, readiness.ready() ? good : readiness.first_failed == CameraStep::count ? foreground : warn);

        // The picture as a mirror shows it (the player's right hand on the
        // right), with the body found in it.
        const int top = 76, gap = 20;
        const uint32_t pw = view.picture.width, ph = view.picture.height;
        if (pw && ph && view.picture.bgra.size() >= size_t(pw) * ph * 4) {
            const int area_w = std::max(0, width - 2 * gap), area_h = std::max(0, height - top - 40);
            const double scale = std::min(double(area_w) / pw, double(area_h) / ph);
            const int w = int(pw * scale), h = int(ph * scale);
            const int left = (width - w) / 2;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = int(pw);
            info.bmiHeader.biHeight = -int(ph);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE);
            // A camera facing the player is turned round to be a mirror; one
            // that already mirrors is shown as it is.
            const bool flip = !view.mirrored;
            StretchDIBits(dc, flip ? left + w - 1 : left, top, flip ? -w : w, h, 0, 0, int(pw), int(ph),
                          view.picture.bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            if (view.found) {
                const auto place = [&](const PoseLandmark& p) {
                    const double px = flip ? double(pw) - p.x : p.x;
                    return POINT{left + LONG(px * scale), top + LONG(p.y * scale)};
                };
                HPEN pen = CreatePen(PS_SOLID, 3, readiness.ready() ? body_colour : warn);
                const auto old_pen = SelectObject(dc, pen);
                for (const auto& bone : bones) {
                    const PoseLandmark& a = view.body[bone[0]];
                    const PoseLandmark& b = view.body[bone[1]];
                    if (!(a.score >= 0.5f) || !(b.score >= 0.5f)) continue;
                    const POINT pa = place(a), pb = place(b);
                    MoveToEx(dc, pa.x, pa.y, nullptr);
                    LineTo(dc, pb.x, pb.y);
                }
                SelectObject(dc, old_pen);
                DeleteObject(pen);
            }
        }
        text(dc, gap, height - 28, say(L"Close this window to go back to the game.", L"關閉此視窗即可回到遊戲。"), dim);
        BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_font);
        SelectObject(dc, old_bitmap);
        if (font) DeleteObject(font);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        switch (message) {
        case WM_TIMER:
            if (self->stopping.stop_requested()) DestroyWindow(window);
            else {
                if (self->player->view(self->view)) self->last_picture = std::chrono::steady_clock::now();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(window, &ps);
            RECT rect;
            GetClientRect(window, &rect);
            self->paint(dc, rect);
            EndPaint(window, &ps);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
            limits->ptMinTrackSize = {640, 480};
            return 0;
        }
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY: self->is_closed.store(true); PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    void run(std::stop_token stop) {
        stopping = stop;
        opened_at = std::chrono::steady_clock::now();
        player->share_view(true);
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure;
        cls.hInstance = instance;
        cls.lpszClassName = L"SfrCameraTuner";
        cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        HWND window = nullptr;
        if (RegisterClassW(&cls) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
            window = CreateWindowExW(WS_EX_TOPMOST, cls.lpszClassName,
                                     chinese ? L"Free Riders - Webcam 調整" : L"Free Riders - Webcam adjustment",
                                     WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 680, nullptr, nullptr,
                                     instance, this);
        if (window && SetTimer(window, 1, 33, nullptr)) {
            ShowWindow(window, SW_SHOW);
            MSG message;
            while (!stop.stop_requested() && GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (window && IsWindow(window)) DestroyWindow(window);
        player->share_view(false);
        is_closed.store(true);
    }
};

CameraTunerWindow::CameraTunerWindow(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CameraTunerWindow::~CameraTunerWindow() = default;

std::unique_ptr<CameraTunerWindow> CameraTunerWindow::open_over(CameraPlayer& player) {
    try {
        auto impl = std::make_unique<Impl>();
        impl->player = &player;
        impl->chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
        auto* raw = impl.get();
        impl->worker = std::jthread([raw](std::stop_token stop) { raw->run(stop); });
        return std::unique_ptr<CameraTunerWindow>(new CameraTunerWindow(std::move(impl)));
    } catch (const std::exception& error) {
        std::cerr << "NATIVE_CAMERA_TUNER unavailable: " << error.what() << '\n';
        return nullptr;
    }
}

bool CameraTunerWindow::closed() const { return impl_->is_closed.load(); }
}
#else
namespace sfr {
struct CameraTunerWindow::Impl {};
CameraTunerWindow::CameraTunerWindow(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CameraTunerWindow::~CameraTunerWindow() = default;
std::unique_ptr<CameraTunerWindow> CameraTunerWindow::open_over(CameraPlayer&) { return nullptr; }
bool CameraTunerWindow::closed() const { return true; }
}
#endif
