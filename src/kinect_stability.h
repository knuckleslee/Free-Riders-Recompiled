#pragma once
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
// ankle carries its hand or foot with it. In the menus the hands then go
// through MenuHandSteadying, so the cursor settles on a button.
//
// Each body keeps its own memory by tracking id; a body that leaves takes it
// with it. SFR_POSE_STABILIZE=0 and SFR_MENU_HAND_STEADY=0 turn either off,
// as they do for the webcam.
class KinectBodySteadying {
public:
    static KinectBodySteadying from_environment();
    KinectBodySteadying(bool stabilize = true, bool steady_menu = true)
        : stabilize_(stabilize), steady_menu_(steady_menu) {}

    // A new frame of bodies, the interval since the frame before.
    void steady(std::vector<KinectBody>& bodies, double interval_seconds, bool in_menu);
    // Points held in the last frame, over every body, for the log.
    uint32_t held() const { return held_; }

    // The confidence a tracking state stands for.
    static float score_of(uint32_t state) { return state >= 2 ? 0.9f : state == 1 ? 0.3f : 0.0f; }

private:
    struct Body {
        uint32_t tracking_id = 0;
        PoseStabilizer stabilizer;
        MenuHandSteadying hands;
        uint64_t last_seen = 0;
    };
    Body& body_for(uint32_t tracking_id);
    bool stabilize_, steady_menu_;
    std::vector<Body> bodies_;
    uint64_t frame_ = 0;
    uint32_t held_ = 0;
};

}
