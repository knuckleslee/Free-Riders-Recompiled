#include "pause_gesture.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using Clock = sfr::PauseGesture::Clock;
namespace joint = sfr::nui_joint;

// A player 2.5 m away, left shoulder at -0.18 (the player's left is -x),
// the left arm 0.55 m long pointing `degrees` below the horizontal, out to
// the side, `forward` metres towards the sensor.
sfr::SkeletonJoints body(float degrees, float bend = 0.0f, float forward = 0.0f) {
    sfr::SkeletonJoints joints{};
    for (auto& j : joints) j = {0, 0, 2.5f};
    joints[joint::shoulder_left] = {-0.18f, 0.5f, 2.5f};
    joints[joint::shoulder_right] = {0.18f, 0.5f, 2.5f};
    const float a = degrees * 3.14159265f / 180.0f;
    const float dx = -std::cos(a), dy = -std::sin(a);
    const auto& s = joints[joint::shoulder_left];
    joints[joint::elbow_left] = {s[0] + dx * 0.28f, s[1] + dy * 0.28f + bend, s[2] - forward * 0.5f};
    joints[joint::hand_left] = {s[0] + dx * 0.55f, s[1] + dy * 0.55f, s[2] - forward};
    joints[joint::hand_right] = {0.25f, 0.0f, 2.5f};
    return joints;
}

Clock::time_point at(double seconds) { return Clock::time_point{} + std::chrono::milliseconds(int64_t(seconds * 1000)); }

void the_pose() {
    require(sfr::PauseGesture::in_pose(body(45)), "the left arm out and down at 45 degrees is the pose");
    require(sfr::PauseGesture::in_pose(body(30)) && sfr::PauseGesture::in_pose(body(60)), "30 to 60 degrees are the pose");
    require(!sfr::PauseGesture::in_pose(body(0)), "an arm straight out sideways is not");
    require(!sfr::PauseGesture::in_pose(body(85)), "an arm hanging down is not");
    require(!sfr::PauseGesture::in_pose(body(45, 0.35f)), "a bent arm is not");
    require(!sfr::PauseGesture::in_pose(body(45, 0.0f, 0.45f)), "an arm pointing at the sensor is not");
    auto right = body(45);
    for (const auto j : {joint::elbow_left, joint::hand_left}) right[j][0] = -right[j][0];
    require(!sfr::PauseGesture::in_pose(right), "the left hand across the body is not");
    auto both = body(45);
    both[joint::hand_right] = {0.6f, 0.2f, 2.5f};
    require(!sfr::PauseGesture::in_pose(both), "with the right arm out as well it is not");
    auto raised = body(45);
    raised[joint::hand_right] = {0.2f, 0.9f, 2.5f};
    require(!sfr::PauseGesture::in_pose(raised), "with the right hand up it is not");
}

void it_fires_once_after_the_hold() {
    sfr::PauseGesture guide(true, 2.0);
    int fired = 0;
    for (double t = 0; t <= 3.0; t += 1.0 / 30.0)
        if (guide.update(body(45), at(t))) {
            ++fired;
            require(t >= 2.0 - 1e-6, "not before two seconds");
        }
    require(fired == 1, "held on, it fires once");
    // The arm comes down, then goes up again: a second time.
    for (double t = 3.0; t <= 3.5; t += 1.0 / 30.0) guide.update(body(85), at(t));
    for (double t = 3.5; t <= 6.0; t += 1.0 / 30.0)
        if (guide.update(body(45), at(t))) ++fired;
    require(fired == 2, "after leaving the pose it may fire again");
}

void a_short_glitch_does_not_restart_the_wait() {
    sfr::PauseGesture guide(true, 2.0);
    bool fired = false;
    for (double t = 0; t <= 2.1 && !fired; t += 1.0 / 30.0) {
        const bool glitch = t > 1.0 && t < 1.1;  // three pictures lost the arm
        fired = guide.update(body(glitch ? 0 : 45), at(t));
    }
    require(fired, "a tenth of a second without the pose is forgiven");

    sfr::PauseGesture again(true, 2.0);
    fired = false;
    for (double t = 0; t <= 2.1 && !fired; t += 1.0 / 30.0) {
        const bool away = t > 1.0 && t < 1.5;
        fired = again.update(body(away ? 0 : 45), at(t));
    }
    require(!fired, "half a second out of the pose starts the wait again");
}

void it_can_be_turned_off() {
    sfr::PauseGesture guide(false);
    bool fired = false;
    for (double t = 0; t <= 3.0; t += 1.0 / 30.0) fired = fired || guide.update(body(45), at(t));
    require(!fired, "SFR_PAUSE_GESTURE=0 never fires");
}
}

int main() {
    try {
        the_pose();
        it_fires_once_after_the_hold();
        a_short_glitch_does_not_restart_the_wait();
        it_can_be_turned_off();
    } catch (const std::exception& error) {
        std::cerr << "pause_gesture_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
