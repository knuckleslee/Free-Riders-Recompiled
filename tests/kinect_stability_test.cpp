#include "kinect_stability.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool near(float a, float b, float within) { return std::fabs(a - b) <= within; }

constexpr double interval = 1.0 / 30.0;
namespace joint = sfr::nui_joint;

// A player standing still 2.4 m from the sensor, arms down (sensor space:
// metres, +y up), every joint tracked.
sfr::KinectBody standing(uint32_t id = 7) {
    sfr::KinectBody body;
    body.tracking_id = id;
    const auto set = [&](uint32_t j, float x, float y) {
        body.joints[j] = {x, y, 2.4f};
        body.joint_states[j] = 2;
    };
    set(joint::hip_center, 0, 0);
    set(joint::spine, 0, 0.2f);
    set(joint::shoulder_center, 0, 0.5f);
    set(joint::head, 0, 0.7f);
    set(joint::shoulder_left, -0.18f, 0.5f);
    set(joint::elbow_left, -0.2f, 0.22f);
    set(joint::wrist_left, -0.21f, -0.03f);
    set(joint::hand_left, -0.21f, -0.1f);
    set(joint::shoulder_right, 0.18f, 0.5f);
    set(joint::elbow_right, 0.2f, 0.22f);
    set(joint::wrist_right, 0.21f, -0.03f);
    set(joint::hand_right, 0.21f, -0.1f);
    set(joint::hip_left, -0.1f, 0);
    set(joint::knee_left, -0.1f, -0.42f);
    set(joint::ankle_left, -0.1f, -0.82f);
    set(joint::foot_left, -0.1f, -0.88f);
    set(joint::hip_right, 0.1f, 0);
    set(joint::knee_right, 0.1f, -0.42f);
    set(joint::ankle_right, 0.1f, -0.82f);
    set(joint::foot_right, 0.1f, -0.88f);
    return body;
}

void settle(sfr::KinectBodySteadying& steadying, bool in_menu = false) {
    for (int i = 0; i < 10; ++i) {
        std::vector<sfr::KinectBody> bodies{standing()};
        steadying.steady(bodies, interval, in_menu);
    }
}

void a_tracked_body_passes_untouched() {
    sfr::KinectBodySteadying steadying;
    for (int i = 0; i < 20; ++i) {
        std::vector<sfr::KinectBody> bodies{standing()};
        const auto given = bodies[0].joints;
        steadying.steady(bodies, interval, false);
        require(steadying.held() == 0 && bodies[0].joints == given, "a fully tracked body is left as it is");
    }
}

void an_inferred_wrist_is_held_and_its_hand_goes_with_it() {
    sfr::KinectBodySteadying steadying;
    settle(steadying);
    // The right hand passes behind the body: inferred, guessed on the other side.
    std::vector<sfr::KinectBody> bodies{standing()};
    auto& body = bodies[0];
    body.joints[joint::wrist_right] = {-0.25f, 0.0f, 2.4f};
    body.joints[joint::hand_right] = {-0.25f, -0.07f, 2.4f};
    body.joint_states[joint::wrist_right] = 1;
    body.joint_states[joint::hand_right] = 1;
    steadying.steady(bodies, interval, false);
    require(steadying.held() == 1, "the inferred wrist is held");
    require(near(body.joints[joint::wrist_right][0], 0.21f, 1e-4f), "it stays on its own side");
    require(near(body.joints[joint::hand_right][0], 0.21f, 1e-4f) && near(body.joints[joint::hand_right][1], -0.1f, 1e-4f),
            "the hand stays with its wrist");
    require(body.joints[joint::wrist_right][2] == 2.4f, "at its depth");
}

void a_forearm_that_grows_is_not_believed_even_tracked() {
    sfr::KinectBodySteadying steadying;
    settle(steadying);
    std::vector<sfr::KinectBody> bodies{standing()};
    bodies[0].joints[joint::wrist_left] = {-0.2f, -0.35f, 2.4f};  // twice the forearm
    steadying.steady(bodies, interval, false);
    require(steadying.held() == 1 && near(bodies[0].joints[joint::wrist_left][1], -0.03f, 1e-4f),
            "a forearm twice its length is held");
}

void each_body_keeps_its_own_memory() {
    sfr::KinectBodySteadying steadying;
    settle(steadying);
    // A second player arrives with a hidden hand on the first frame they are
    // seen: nothing known about them to hold from, so their point is used.
    std::vector<sfr::KinectBody> bodies{standing(7), standing(9)};
    bodies[1].joints[joint::wrist_right] = {-0.25f, 0.0f, 2.4f};
    bodies[1].joint_states[joint::wrist_right] = 1;
    steadying.steady(bodies, interval, false);
    require(steadying.held() == 0 && near(bodies[1].joints[joint::wrist_right][0], -0.25f, 1e-4f),
            "a new body is not held to another body's arm");
}

void the_menu_cursor_settles() {
    sfr::KinectBodySteadying steadying;
    float worst = 0;
    for (int step = 0; step < 60; ++step) {
        std::vector<sfr::KinectBody> bodies{standing()};
        auto& hand = bodies[0].joints[joint::hand_right];
        hand = {0.3f + 0.004f * std::sin(float(step) * 2.7f), 0.6f + 0.003f * std::cos(float(step) * 1.9f), 2.1f};
        bodies[0].joints[joint::wrist_right] = {hand[0], hand[1] - 0.05f, 2.15f};
        steadying.steady(bodies, interval, true);
        if (step) worst = std::fmax(worst, std::fabs(bodies[0].joints[joint::hand_right][0] - 0.3f));
    }
    require(worst <= 0.0045f, "a hand held on a button holds the cursor");
}

void both_can_be_turned_off() {
    sfr::KinectBodySteadying steadying(false, false);
    settle(steadying);
    std::vector<sfr::KinectBody> bodies{standing()};
    bodies[0].joints[joint::wrist_right] = {-0.25f, 0.0f, 2.4f};
    bodies[0].joint_states[joint::wrist_right] = 1;
    steadying.steady(bodies, interval, true);
    require(bodies[0].joints[joint::wrist_right][0] == -0.25f, "switched off, the sensor's points are used");
}
}

int main() {
    try {
        a_tracked_body_passes_untouched();
        an_inferred_wrist_is_held_and_its_hand_goes_with_it();
        a_forearm_that_grows_is_not_believed_even_tracked();
        each_body_keeps_its_own_memory();
        the_menu_cursor_settles();
        both_can_be_turned_off();
    } catch (const std::exception& error) {
        std::cerr << "kinect_stability_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
