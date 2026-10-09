#pragma once
#include "pose_skeleton.h"

#include <chrono>
#include <cstdint>

namespace sfr {

// The Kinect Guide gesture of the Xbox 360: the left arm held straight out
// and down, about 45 degrees from the body, for a moment. On the console it
// is the system's, not the title's: it opens the Kinect Guide over any game
// and the game pauses. Here it pauses a race the way the voice command does
// (the title's "pauseopen"), from a Kinect's skeleton or the webcam's.
//
// Skeleton space as the title has it: metres, +y up, and the player's right
// is +x, so the left hand reaching out goes towards -x. The arm counts as in
// the pose when
//   - it is nearly straight: shoulder to hand is at least 85% of the upper
//     arm and forearm together;
//   - the hand is out to the side and below the shoulder, 25 to 65 degrees
//     under the horizontal;
//   - it points sideways rather than at the sensor: the hand is no more
//     than half the arm's length in front of or behind the shoulder.
// Held for hold_seconds it fires once, and fires again only after the arm
// has left the pose. A glitch shorter than grace_seconds (a picture that
// lost the arm) does not start the wait again.
class GuideGesture {
public:
    using Clock = std::chrono::steady_clock;
    // SFR_GUIDE_GESTURE=0 turns it off; SFR_GUIDE_GESTURE_SECONDS sets how
    // long to hold (default 2, as the console's).
    static GuideGesture from_environment();
    explicit GuideGesture(bool enabled = true, double hold_seconds = 2.0)
        : enabled_(enabled), hold_seconds_(hold_seconds) {}

    // True once, at the moment the pose has been held long enough.
    bool update(const SkeletonJoints& joints, Clock::time_point now);
    void forget();
    // 0 to 1 while the pose is held, for the log.
    float progress(Clock::time_point now) const;

    static bool in_pose(const SkeletonJoints& joints);

    static constexpr double grace_seconds = 0.2;

private:
    bool enabled_;
    double hold_seconds_;
    bool holding_ = false, fired_ = false;
    Clock::time_point since_{}, last_seen_{};
};

}
