#include "kinect_stability.h"

#include <cstdlib>

namespace sfr {
namespace {
// The sensor's joints a webcam body has too, in the same places.
struct Shared {
    uint32_t pose, joint;
};
constexpr std::array<Shared, 12> shared{{
    {pose_point::shoulder_left, nui_joint::shoulder_left}, {pose_point::shoulder_right, nui_joint::shoulder_right},
    {pose_point::elbow_left, nui_joint::elbow_left}, {pose_point::elbow_right, nui_joint::elbow_right},
    {pose_point::wrist_left, nui_joint::wrist_left}, {pose_point::wrist_right, nui_joint::wrist_right},
    {pose_point::hip_left, nui_joint::hip_left}, {pose_point::hip_right, nui_joint::hip_right},
    {pose_point::knee_left, nui_joint::knee_left}, {pose_point::knee_right, nui_joint::knee_right},
    {pose_point::ankle_left, nui_joint::ankle_left}, {pose_point::ankle_right, nui_joint::ankle_right},
}};
// The end of a limb the stabilizer does not check, and the point it hangs from.
constexpr std::array<std::array<uint32_t, 2>, 4> ends{{
    {nui_joint::hand_left, nui_joint::wrist_left}, {nui_joint::hand_right, nui_joint::wrist_right},
    {nui_joint::foot_left, nui_joint::ankle_left}, {nui_joint::foot_right, nui_joint::ankle_right},
}};
bool switched_off(const char* name) {
    const char* const text = std::getenv(name);
    return text && *text == '0';
}
}

KinectBodySteadying KinectBodySteadying::from_environment() {
    return KinectBodySteadying(!switched_off("SFR_POSE_STABILIZE"), !switched_off("SFR_MENU_HAND_STEADY"));
}

KinectBodySteadying::Body& KinectBodySteadying::body_for(uint32_t tracking_id) {
    for (Body& body : bodies_)
        if (body.tracking_id == tracking_id) return body;
    bodies_.push_back(Body{tracking_id, PoseStabilizer(stabilize_), MenuHandSteadying(steady_menu_), 0});
    return bodies_.back();
}

void KinectBodySteadying::steady(std::vector<KinectBody>& bodies, double interval, bool in_menu) {
    held_ = 0;
    ++frame_;
    if (!stabilize_ && !steady_menu_) return;
    for (KinectBody& body : bodies) {
        if (!body.tracking_id) continue;
        Body& memory = body_for(body.tracking_id);
        memory.last_seen = frame_;
        if (stabilize_) {
            // The sensor's metres as the stabilizer reads them: lengths and
            // distances, which do not care that the sensor's +y is up.
            PoseLandmarks points{};
            for (const Shared& s : shared) {
                PoseLandmark& point = points[s.pose];
                point.world = body.joints[s.joint];
                point.has_world = true;
                point.x = point.world[0];
                point.y = point.world[1];
                point.score = score_of(body.joint_states[s.joint]);
            }
            memory.stabilizer.stabilize(points, interval);
            held_ += memory.stabilizer.held();
            const SkeletonJoints given = body.joints;
            for (const Shared& s : shared) body.joints[s.joint] = points[s.pose].world;
            for (const auto& end : ends)
                for (int axis = 0; axis < 3; ++axis)
                    body.joints[end[0]][axis] += body.joints[end[1]][axis] - given[end[1]][axis];
        }
        memory.hands.steady(body.joints, in_menu);
    }
    // A body gone for a second is somebody else when that id comes back.
    std::erase_if(bodies_, [&](const Body& body) { return frame_ - body.last_seen > 30; });
}

}
