#include "kinect_stability.h"
#include "kinect_depth_scene.h"

#include <cmath>
#include <cstdlib>
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

void a_wrist_hidden_for_seconds_follows_slowly() {
    sfr::KinectBodySteadying steadying;
    settle(steadying);
    // Two seconds inferred, the guess swapping sides every few frames: past
    // the hold the wrist follows the guesses, never faster than it may.
    float last = 0.21f, worst = 0;
    for (int i = 0; i < 60; ++i) {
        std::vector<sfr::KinectBody> bodies{standing()};
        auto& wrist = bodies[0].joints[joint::wrist_right];
        wrist = {i % 5 < 2 ? -0.25f : 0.21f, i % 5 < 2 ? 0.0f : -0.03f, 2.4f};
        bodies[0].joint_states[joint::wrist_right] = 1;
        steadying.steady(bodies, interval, false);
        const float x = bodies[0].joints[joint::wrist_right][0];
        worst = std::fmax(worst, std::fabs(x - last));
        last = x;
    }
    require(worst <= sfr::KinectBodySteadying::follow_speed * float(interval) + 1e-4f,
            "a long-hidden wrist never jumps to a guess");
    // Seen again: believed at once.
    std::vector<sfr::KinectBody> bodies{standing()};
    steadying.steady(bodies, interval, false);
    require(near(bodies[0].joints[joint::wrist_right][0], 0.21f, 1e-4f), "a wrist seen again is where the sensor sees it");
}

// The depth check (kinect_depth_layer.h): the image is drawn from the body
// as it really stands, the skeleton handed on is the sensor's.
sfr::KinectImage depth_of(const sfr::KinectBody& real) {
    sfr_test::DepthScene scene;
    scene.body(real);
    return scene.image;
}

// The right arm half raised, the wrist and hand well before the chest.
sfr::KinectBody arm_forward(uint32_t state) {
    sfr::KinectBody body = standing();
    body.joints[joint::wrist_right] = {0.12f, 0.08f, 2.18f};
    body.joints[joint::hand_right] = {0.1f, 0.12f, 2.12f};
    body.joint_states[joint::wrist_right] = body.joint_states[joint::hand_right] = state;
    return body;
}

void an_inferred_wrist_the_image_shows_is_believed_at_once() {
    // Without the image the inferred wrist is held, as before.
    {
        sfr::KinectBodySteadying steadying;
        settle(steadying);
        std::vector<sfr::KinectBody> bodies{arm_forward(1)};
        steadying.steady(bodies, interval, false);
        require(steadying.held() == 1, "without depth an inferred wrist is held");
    }
    sfr::KinectBodySteadying steadying;
    settle(steadying);
    const sfr::KinectImage depth = depth_of(arm_forward(2));
    std::vector<sfr::KinectBody> bodies{arm_forward(1)};
    steadying.steady(bodies, interval, false, &depth);
    require(steadying.held() == 0, "an inferred wrist the image shows is not held");
    require(near(bodies[0].joints[joint::wrist_right][2], 2.18f, 1e-4f), "it is where the sensor put it");
    require(steadying.depth_counts().raised == 1 && steadying.depth_counts().seen >= 1, "and counted as raised");
}

void an_inferred_wrist_guessed_before_the_chest_is_still_held() {
    sfr::KinectBodySteadying steadying;
    settle(steadying);
    // The arm really hangs at the side; the guess is before the belly.
    const sfr::KinectImage depth = depth_of(standing());
    std::vector<sfr::KinectBody> bodies{arm_forward(1)};
    steadying.steady(bodies, interval, false, &depth);
    require(steadying.held() == 1, "a guess the image contradicts is held");
    require(steadying.depth_counts().impossible >= 1 && steadying.depth_counts().raised == 0, "and counted as impossible");
}

// The right arm out to the side, clear of the body.
sfr::KinectBody arm_out(float wrist_x = 0.55f) {
    sfr::KinectBody body = standing();
    body.joints[joint::elbow_right] = {0.38f, 0.42f, 2.4f};
    body.joints[joint::wrist_right] = {wrist_x, 0.32f, 2.4f};
    body.joints[joint::hand_right] = {wrist_x + 0.07f, 0.3f, 2.4f};
    return body;
}

void a_tracked_wrist_with_nothing_there_is_held() {
    sfr::KinectBodySteadying steadying;
    for (int i = 0; i < 10; ++i) {
        std::vector<sfr::KinectBody> bodies{arm_out()};
        const sfr::KinectImage depth = depth_of(arm_out());
        steadying.steady(bodies, interval, false, &depth);
        require(steadying.held() == 0 && steadying.depth_counts().lowered == 0, "an arm the image shows is never held");
    }
    // The arm really dropped; the sensor keeps a tracked elbow and wrist in
    // the air.
    const sfr::KinectImage depth = depth_of(standing());
    std::vector<sfr::KinectBody> bodies{arm_out(0.555f)};
    steadying.steady(bodies, interval, false, &depth);
    require(steadying.depth_counts().lowered == 2 && steadying.depth_counts().elsewhere == 3,
            "the tracked elbow and wrist in the air are doubted");
    require(steadying.held() == 2 && near(bodies[0].joints[joint::wrist_right][0], 0.55f, 1e-4f), "and held");
}

void a_tracked_wrist_moving_fast_is_not_doubted() {
    sfr::KinectBodySteadying steadying;
    for (int i = 0; i < 10; ++i) {
        std::vector<sfr::KinectBody> bodies{arm_out()};
        const sfr::KinectImage depth = depth_of(arm_out());
        steadying.steady(bodies, interval, false, &depth);
    }
    // 8 cm in a frame (2.4 m/s): the image, a frame or two old, may show it
    // where it was. Not held; the still elbow beside it is.
    const sfr::KinectImage depth = depth_of(standing());
    std::vector<sfr::KinectBody> bodies{arm_out(0.63f)};
    steadying.steady(bodies, interval, false, &depth);
    require(steadying.depth_counts().lowered == 1 && steadying.held() == 1, "only the still elbow is doubted");
    require(near(bodies[0].joints[joint::wrist_right][0], 0.63f, 1e-4f), "a fast tracked wrist is not doubted");
}

// The scripted frames of the inferred-wrist and hidden-for-seconds tests.
sfr::KinectBody scripted(int i) {
    sfr::KinectBody body = standing();
    if (i >= 10) {
        auto& wrist = body.joints[joint::wrist_right];
        wrist = {i % 5 < 2 ? -0.25f : 0.21f, i % 5 < 2 ? 0.0f : -0.03f, 2.4f};
        body.joint_states[joint::wrist_right] = 1;
    }
    if (i >= 50) body.joints[joint::wrist_left] = {-0.2f, -0.35f, 2.4f};
    return body;
}

void without_a_usable_image_nothing_changes() {
    sfr::KinectBodySteadying plain, off(true, true, false), blank, shifted, other_size;
    const sfr::KinectImage empty = sfr_test::DepthScene(0).image;
    sfr::KinectBody aside = standing();
    for (auto& p : aside.joints) p[0] += 0.7f;
    const sfr::KinectImage misaligned = depth_of(aside);
    sfr::KinectImage resized = depth_of(standing());
    resized.width = 160;
    for (int i = 0; i < 70; ++i) {
        std::vector<sfr::KinectBody> a{scripted(i)}, b = a, c = a, d = a, e = a;
        const sfr::KinectImage depth = depth_of(standing());
        plain.steady(a, interval, i > 60);
        off.steady(b, interval, i > 60, &depth);
        blank.steady(c, interval, i > 60, &empty);
        shifted.steady(d, interval, i > 60, &misaligned);
        other_size.steady(e, interval, i > 60, &resized);
        require(a[0].joints == b[0].joints && a[0].joints == c[0].joints && a[0].joints == d[0].joints &&
                    a[0].joints == e[0].joints,
                "without a usable image the joints are as before");
        require(plain.held() == off.held() && plain.held() == blank.held() && plain.held() == shifted.held(),
                "and so are the holds");
        require(plain.depth_counts().seen + plain.depth_counts().unknown + off.depth_counts().seen == 0,
                "nothing is counted without the check");
        require(blank.depth_counts().unmatched == 1 && shifted.depth_counts().unmatched == 1 &&
                    blank.depth_counts().raised + blank.depth_counts().lowered == 0,
                "an image that does not match the body is counted as such and changes nothing");
    }
}

void the_depth_check_can_be_turned_off() {
#ifdef _WIN32
    _putenv_s("SFR_KINECT_DEPTH_LAYER", "0");
#else
    setenv("SFR_KINECT_DEPTH_LAYER", "0", 1);
#endif
    sfr::KinectBodySteadying steadying = sfr::KinectBodySteadying::from_environment();
    settle(steadying);
    const sfr::KinectImage depth = depth_of(arm_forward(2));
    std::vector<sfr::KinectBody> bodies{arm_forward(1)};
    steadying.steady(bodies, interval, false, &depth);
    require(steadying.held() == 1 && steadying.depth_counts().seen == 0, "SFR_KINECT_DEPTH_LAYER=0 ignores the image");
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
        a_wrist_hidden_for_seconds_follows_slowly();
        the_menu_cursor_settles();
        both_can_be_turned_off();
        an_inferred_wrist_the_image_shows_is_believed_at_once();
        an_inferred_wrist_guessed_before_the_chest_is_still_held();
        a_tracked_wrist_with_nothing_there_is_held();
        a_tracked_wrist_moving_fast_is_not_doubted();
        without_a_usable_image_nothing_changes();
        the_depth_check_can_be_turned_off();
    } catch (const std::exception& error) {
        std::cerr << "kinect_stability_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
