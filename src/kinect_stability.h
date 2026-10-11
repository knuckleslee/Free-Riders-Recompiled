#pragma once
#include "kinect_depth_layer.h"
#include "kinect_sensor.h"
#include "pose_stability.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sfr {

// The webcam's two checks (pose_stability.h), on the Kinect's skeletons.
//
// The sensor's skeleton is better than a camera model's, but not when a limb
// is hidden: an arm across the body or a hand behind it is still given a
// place, marked inferred, and that place jumps, folds the arm the wrong way
// or swaps sides. So each body's limb points go through the same
// PoseStabilizer, with the sensor's own tracking state as the confidence
// (tracked is believed, inferred or not tracked is not), and a held wrist or
// ankle carries its hand or foot with it. A joint inferred for longer than
// the hold then follows the sensor's guesses slowly, one frame's jump
// ignored, instead of taking them as they come. In the menus the hands then go
// through MenuHandSteadying, so the cursor settles on a button.
//
// When the sensor's depth image comes with the frame, each limb point is
// first checked against it (kinect_depth_layer.h), and the verdict tells the
// stabilizer more than the tracking state can:
//   - an inferred point the image shows in plain view, where the sensor put
//     it, is believed (seen_score) instead of held: the SDK only lost
//     confidence in it, it did not lose the limb;
//   - a tracked point with nothing of the player where the sensor puts it,
//     or put in front of the player's own body where the image sees the
//     body, is not believed: the SDK has latched onto something else. Only
//     while the point is nearly still, though: the image may be a frame
//     older than the skeleton, and a hand moving fast is honestly somewhere
//     the older image does not show it;
//   - hidden and unknown points keep the confidence of their state.
// No image, an image of another size, or one whose player pixels do not
// line up with the body's trunk changes nothing.
//
// Each body keeps its own memory by tracking id; a body that leaves takes it
// with it. SFR_POSE_STABILIZE=0 and SFR_MENU_HAND_STEADY=0 turn either off,
// as they do for the webcam; SFR_KINECT_DEPTH_LAYER=0 turns the depth check
// off.
class KinectBodySteadying {
public:
    static KinectBodySteadying from_environment();
    KinectBodySteadying(bool stabilize = true, bool steady_menu = true, bool depth_layer = true)
        : stabilize_(stabilize), steady_menu_(steady_menu), depth_layer_(depth_layer) {}

    // A new frame of bodies, the interval since the frame before, and the
    // newest depth and player image if there is one (it may be a frame
    // behind the skeleton).
    void steady(std::vector<KinectBody>& bodies, double interval_seconds, bool in_menu,
                const KinectImage* depth = nullptr);
    // Points held in the last frame, over every body, for the log.
    uint32_t held() const { return held_; }
    // What the depth check found in the last frame, over every body.
    const KinectDepthCounts& depth_counts() const { return depth_counts_; }

    // An arm hidden for longer than a hold (crossed all race long) is still
    // only guessed: it follows the guesses at up to this speed, m/s.
    static constexpr float follow_speed = 1.5f;

    // The confidence a tracking state stands for.
    static float score_of(uint32_t state) { return state >= 2 ? 0.9f : state == 1 ? 0.3f : 0.0f; }
    // An inferred point the depth image shows: believed (above the
    // stabilizer's min_score), but below the 0.7 its bone lengths are
    // learned from, which stays the tracked points' alone.
    static constexpr float seen_score = 0.6f;
    // A tracked point is doubted by the depth image only while moving slower
    // than this, m/s: at 30 frames a second an image two frames late is then
    // no more than 7 cm behind, which the check's ring of samples covers.
    static constexpr float doubt_speed = 1.0f;

private:
    struct Body {
        uint32_t tracking_id = 0;
        PoseStabilizer stabilizer;
        MenuHandSteadying hands;
        uint64_t last_seen = 0;
        SkeletonJoints given{};  // the sensor's joints last frame, for their speed
    };
    Body& body_for(uint32_t tracking_id);
    bool stabilize_, steady_menu_, depth_layer_;
    std::vector<Body> bodies_;
    uint64_t frame_ = 0;
    uint32_t held_ = 0;
    KinectDepthCounts depth_counts_{};
};

}
