#include "kinect_preview.h"

namespace sfr {
// NuiImageCamera.h: NUI_CAMERA_DEPTH_NOMINAL_FOCAL_LENGTH_IN_PIXELS (at
// 320x240) and NUI_CAMERA_COLOR_NOMINAL_FOCAL_LENGTH_IN_PIXELS (at 640x480).
KinectImagePoint kinect_depth_point(const std::array<float, 3>& p) {
    if (!(p[2] > 0.1f)) return {};
    return {160.0f + p[0] * 285.63f / p[2], 120.0f - p[1] * 285.63f / p[2], true};
}
KinectImagePoint kinect_colour_point(const std::array<float, 3>& p) {
    if (!(p[2] > 0.1f)) return {};
    return {320.0f + p[0] * 531.15f / p[2], 240.0f - p[1] * 531.15f / p[2], true};
}

KinectReadiness kinect_readiness(const KinectReadinessInput& input) {
    KinectReadiness result;
    auto& steps = result.steps;
    const auto set = [&](KinectStep step, KinectStepState state) {
        steps[size_t(step)] = state;
        if (state == KinectStepState::failed && result.first_failed == KinectStep::count) result.first_failed = step;
    };
    if (!input.opened) {
        if (input.failure.empty()) return result;  // still opening: everything waits
        // Without a runtime there is nothing to ask for a sensor; with one,
        // any other reason is the sensor's (absent, unpowered, or in use).
        const bool no_runtime = input.failure == "no-runtime" || input.failure == "incomplete-runtime" ||
                                input.failure == "unsupported-platform";
        if (no_runtime) set(KinectStep::runtime, KinectStepState::failed);
        else {
            set(KinectStep::runtime, KinectStepState::passed);
            set(KinectStep::sensor, KinectStepState::failed);
        }
        return result;
    }
    set(KinectStep::runtime, KinectStepState::passed);
    set(KinectStep::sensor, KinectStepState::passed);
    const bool arriving = input.seconds_since_frame >= 0 && input.seconds_since_frame <= kinect_frame_timeout_seconds;
    if (!arriving) {
        // A sensor takes a moment to send its first frame; one that has sent
        // some and stopped has failed at once.
        const bool late = input.seconds_since_frame >= 0 || input.seconds_open > kinect_first_frame_seconds;
        if (late) set(KinectStep::frames, KinectStepState::failed);
        return result;
    }
    set(KinectStep::frames, KinectStepState::passed);
    set(KinectStep::body, input.bodies ? KinectStepState::passed : KinectStepState::failed);
    return result;
}
}
