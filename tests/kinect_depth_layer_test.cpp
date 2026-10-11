#include "kinect_depth_layer.h"
#include "kinect_depth_scene.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
namespace joint = sfr::nui_joint;
using Verdict = sfr::KinectDepthVerdict;
using sfr_test::DepthScene;

// A player standing at distance z, arms down, every joint tracked, in the
// sensor's first slot (player index 1) unless told otherwise.
sfr::KinectBody standing(float x0 = 0, float z = 2.4f, uint32_t slot = 0) {
    sfr::KinectBody body;
    body.tracking_id = 7 + slot;
    body.sensor_index = slot;
    const auto set = [&](uint32_t j, float x, float y) {
        body.joints[j] = {x0 + x, y, z};
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

// The right arm reaching forward across the chest, the hand 30 cm before it.
void reach_forward(sfr::KinectBody& body) {
    const float x0 = body.joints[joint::spine][0], z = body.joints[joint::spine][2];
    body.joints[joint::elbow_right] = {x0 + 0.2f, 0.25f, z - 0.15f};
    body.joints[joint::wrist_right] = {x0 + 0.08f, 0.22f, z - 0.27f};
    body.joints[joint::hand_right] = {x0 + 0.04f, 0.22f, z - 0.32f};
}

Verdict verdict(const DepthScene& scene, const sfr::KinectBody& body, uint32_t j) {
    return sfr::kinect_depth_verdict(scene.image, body.sensor_index + 1, body.joints[j]);
}

void a_hand_in_front_of_the_chest_is_seen() {
    sfr::KinectBody body = standing();
    reach_forward(body);
    DepthScene scene;
    scene.body(body);
    require(verdict(scene, body, joint::hand_right) == Verdict::seen, "the hand before the chest is seen");
    require(verdict(scene, body, joint::wrist_right) == Verdict::seen, "so is its wrist");
    require(verdict(scene, body, joint::elbow_right) == Verdict::seen, "and its elbow");
    // Arms at the sides, legs: in plain view too.
    for (uint32_t j : {joint::wrist_left, joint::hand_left, joint::elbow_left, joint::knee_left, joint::ankle_right})
        require(verdict(scene, body, j) == Verdict::seen, "a limb in plain view is seen");
}

void a_far_player_and_a_noisy_image_still_show_the_hand() {
    sfr::KinectBody body = standing(0.3f, 3.8f);
    reach_forward(body);
    DepthScene scene(0);  // the wall out of range
    scene.body(body);
    // Every third pixel lost, as around an edge or on a dark sleeve.
    for (int y = 0; y < DepthScene::height; ++y)
        for (int x = (y % 3); x < DepthScene::width; x += 3) scene.set(x, y, 0, 0);
    require(sfr::kinect_depth_matches_body(scene.image, body), "a far trunk with holes still matches");
    require(verdict(scene, body, joint::hand_right) == Verdict::seen, "a far hand with holes is seen");
}

void an_inferred_hand_guessed_before_the_chest_is_impossible() {
    // The arm really hangs at the side; the sensor guesses the hand in front
    // of the belly, where the image sees only the belly 20 cm farther away.
    const sfr::KinectBody real = standing();
    DepthScene scene;
    scene.body(real);
    sfr::KinectBody guess = real;
    guess.joints[joint::wrist_right] = {0.02f, 0.15f, 2.12f};
    guess.joints[joint::hand_right] = {0.0f, 0.12f, 2.08f};
    guess.joint_states[joint::wrist_right] = guess.joint_states[joint::hand_right] = 1;
    require(verdict(scene, guess, joint::hand_right) == Verdict::impossible, "a hand before the visible chest is impossible");
    require(verdict(scene, guess, joint::wrist_right) == Verdict::impossible, "so is its wrist");
}

void a_hand_behind_the_back_is_hidden() {
    sfr::KinectBody real = standing();
    real.joints[joint::wrist_right] = {0.05f, 0.05f, 2.6f};
    real.joints[joint::hand_right] = {0.0f, 0.05f, 2.62f};
    real.joints[joint::elbow_right] = {0.22f, 0.2f, 2.5f};
    DepthScene scene;
    scene.body(real);  // the trunk in front of it covers it
    require(verdict(scene, real, joint::hand_right) == Verdict::hidden, "a hand behind the trunk is hidden");
    require(verdict(scene, real, joint::wrist_right) == Verdict::hidden, "so is its wrist");
    // The sensor guessing it beside the hip instead, at the body's depth:
    // the trunk's front there is 8 cm nearer, which is not the wrist.
    sfr::KinectBody guess = real;
    guess.joints[joint::wrist_right] = {0.21f, -0.03f, 2.4f};
    require(verdict(scene, guess, joint::wrist_right) != Verdict::seen, "a wrist guessed beside the trunk is not seen");
}

void a_hand_guessed_in_the_air_is_elsewhere() {
    const sfr::KinectBody real = standing();
    DepthScene scene;
    scene.body(real);
    sfr::KinectBody guess = real;
    guess.joints[joint::hand_right] = {0.65f, 0.3f, 2.4f};
    guess.joints[joint::wrist_right] = {0.6f, 0.25f, 2.4f};
    require(verdict(scene, guess, joint::hand_right) == Verdict::elsewhere, "a hand in the air before the wall is elsewhere");
    require(verdict(scene, guess, joint::wrist_right) == Verdict::elsewhere, "so is its wrist");
    // Before a wall out of range there is nothing to say.
    DepthScene empty_wall(0);
    empty_wall.body(real);
    require(verdict(empty_wall, guess, joint::hand_right) == Verdict::unknown, "no depth behind it, no verdict");
}

void off_the_image_or_without_an_image_is_unknown() {
    const sfr::KinectBody body = standing();
    DepthScene scene;
    scene.body(body);
    require(sfr::kinect_depth_verdict(scene.image, 1, {3.0f, 0.0f, 2.4f}) == Verdict::unknown, "off the image");
    require(sfr::kinect_depth_verdict(scene.image, 1, {0.0f, 0.0f, 0.05f}) == Verdict::unknown, "at the sensor");
    require(sfr::kinect_depth_verdict(scene.image, 0, body.joints[joint::hand_left]) == Verdict::unknown,
            "nobody's player index");
    sfr::KinectImage none;
    require(sfr::kinect_depth_verdict(none, 1, body.joints[joint::hand_left]) == Verdict::unknown, "no image");
    sfr::KinectImage colour = scene.image;
    colour.width = 640;
    require(!sfr::kinect_depth_image_usable(colour), "another size is not used");
    DepthScene blank(0);
    require(sfr::kinect_depth_verdict(blank.image, 1, body.joints[joint::hand_left]) == Verdict::unknown,
            "an image without depth");
    const auto layers = sfr::kinect_depth_layers(blank.image, body);
    require(!layers.checked, "a body not in the image is not checked");
    for (Verdict v : layers.verdicts) require(v == Verdict::unknown, "and has no verdicts");
}

void another_players_pixels_are_not_this_player() {
    const sfr::KinectBody me = standing(-0.5f, 2.4f, 0);
    const sfr::KinectBody other = standing(0.5f, 2.4f, 1);
    DepthScene scene;
    scene.body(me);
    scene.body(other);
    sfr::KinectBody guess = me;
    // Guessed on the other player's chest at its depth: something is there,
    // but not this player; it may be the hand touching it. No verdict.
    guess.joints[joint::hand_right] = {0.5f, 0.25f, 2.32f};
    require(verdict(scene, guess, joint::hand_right) == Verdict::unknown, "on someone else at their depth: no verdict");
    // In front of the other player, where the image sees only them.
    guess.joints[joint::hand_right] = {0.5f, 0.25f, 2.0f};
    require(verdict(scene, guess, joint::hand_right) == Verdict::elsewhere, "before someone else: elsewhere");
    // That player's own pixels are theirs.
    require(verdict(scene, other, joint::hand_left) == Verdict::seen, "the other player's hand is seen for them");
    // A third standing in front, covering where the hand is.
    sfr::KinectBody real = standing(-0.5f, 2.4f, 0);
    DepthScene crowded;
    crowded.body(real);
    crowded.box(-0.3f, -0.2f, 0.1f, 0.5f, 1.8f, 3);
    require(verdict(crowded, real, joint::hand_right) == Verdict::hidden, "behind someone nearer: hidden");
}

void the_trunk_must_match_the_image() {
    const sfr::KinectBody body = standing();
    DepthScene scene;
    scene.body(body);
    require(sfr::kinect_depth_matches_body(scene.image, body), "the drawn body matches");
    require(sfr::kinect_depth_layers(scene.image, body).checked, "and is checked");
    sfr::KinectBody wrong_slot = body;
    wrong_slot.sensor_index = 2;
    require(!sfr::kinect_depth_matches_body(scene.image, wrong_slot), "another player index does not match");
    const sfr::KinectBody moved = standing(0.6f);
    require(!sfr::kinect_depth_matches_body(scene.image, moved), "a body 60 cm aside does not match");
    sfr::KinectBody nearer = body;
    for (auto& p : nearer.joints) p[2] -= 0.5f;
    require(!sfr::kinect_depth_matches_body(scene.image, nearer), "a body 50 cm nearer does not match");
    sfr::KinectBody untracked = body;
    untracked.joint_states[joint::spine] = untracked.joint_states[joint::shoulder_center] = 1;
    require(!sfr::kinect_depth_matches_body(scene.image, untracked), "a trunk only guessed is no anchor");
}

void counts_add_up() {
    sfr::KinectDepthCounts a, b;
    a.add(Verdict::seen);
    a.add(Verdict::impossible);
    b.add(Verdict::elsewhere);
    b.add(Verdict::unknown);
    b.add(Verdict::hidden);
    b.raised = 2;
    a += b;
    require(a.seen == 1 && a.hidden == 1 && a.impossible == 1 && a.elsewhere == 1 && a.unknown == 1 && a.raised == 2,
            "counts add up");
}
}

int main() {
    try {
        a_hand_in_front_of_the_chest_is_seen();
        a_far_player_and_a_noisy_image_still_show_the_hand();
        an_inferred_hand_guessed_before_the_chest_is_impossible();
        a_hand_behind_the_back_is_hidden();
        a_hand_guessed_in_the_air_is_elsewhere();
        off_the_image_or_without_an_image_is_unknown();
        another_players_pixels_are_not_this_player();
        the_trunk_must_match_the_image();
        counts_add_up();
    } catch (const std::exception& error) {
        std::cerr << "kinect_depth_layer_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
