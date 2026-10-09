#include "guide_gesture.h"

#include <cmath>
#include <cstdlib>

namespace sfr {

GuideGesture GuideGesture::from_environment() {
    const char* const off = std::getenv("SFR_GUIDE_GESTURE");
    double seconds = 2.0;
    if (const char* text = std::getenv("SFR_GUIDE_GESTURE_SECONDS"); text && *text) {
        char* end = nullptr;
        const double value = std::strtod(text, &end);
        if (end != text && value >= 0.5 && value <= 10.0) seconds = value;
    }
    return GuideGesture(!(off && *off == '0'), seconds);
}

bool GuideGesture::in_pose(const SkeletonJoints& joints) {
    namespace joint = nui_joint;
    const auto& shoulder = joints[joint::shoulder_left];
    const auto& elbow = joints[joint::elbow_left];
    const auto& hand = joints[joint::hand_left];
    const auto distance = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
    };
    const float reach = distance(shoulder, hand);
    const float arm = distance(shoulder, elbow) + distance(elbow, hand);
    if (!std::isfinite(reach) || !std::isfinite(arm) || arm < 0.2f) return false;
    if (reach < 0.85f * arm) return false;  // bent
    const float outward = shoulder[0] - hand[0];  // the player's left is -x
    const float down = shoulder[1] - hand[1];
    if (!(outward > 0) || !(down > 0)) return false;
    const float below = std::atan2(down, outward) * 180.0f / 3.14159265f;
    if (below < 25.0f || below > 65.0f) return false;
    return std::fabs(hand[2] - shoulder[2]) <= 0.5f * reach;  // sideways, not at the sensor
}

void GuideGesture::forget() {
    holding_ = false;
    fired_ = false;
}

bool GuideGesture::update(const SkeletonJoints& joints, Clock::time_point now) {
    if (!enabled_) return false;
    if (in_pose(joints)) {
        if (!holding_) {
            holding_ = true;
            since_ = now;
        }
        last_seen_ = now;
    } else if (holding_ && now - last_seen_ > std::chrono::duration<double>(grace_seconds)) {
        holding_ = false;
        fired_ = false;  // the arm left the pose: it may fire again
    }
    if (!holding_ || fired_) return false;
    if (now - since_ < std::chrono::duration<double>(hold_seconds_)) return false;
    fired_ = true;
    return true;
}

float GuideGesture::progress(Clock::time_point now) const {
    if (!holding_ || fired_) return 0.0f;
    const double held = std::chrono::duration<double>(now - since_).count();
    return float(held >= hold_seconds_ ? 1.0 : held / hold_seconds_);
}

}
