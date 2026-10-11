#pragma once

namespace sfr {
// F8 in the game window flips the menu steadying (menu_steadying in
// pose_stability.h) while playing, so it can be compared on and off. The
// window's title says the frame rate and, playing with a body, which it is.
// Called from the input update; nothing where there is no window to press it
// in.
void poll_menu_steady_hotkey();
}
