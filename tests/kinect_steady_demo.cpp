// The Kinect's limb holding and menu steadying (kinect_stability.h), off and
// on, over a scripted player: what the sensor gives when an arm is hidden or
// a hand is held on a button, made up but shaped like what it does (an
// inferred joint jumping, folding the arm, swapping sides; a still hand
// wandering a few millimetres with an odd wrong frame). Prints one JSON line
// a frame: the scene, whether a menu is up, and every joint as given and as
// steadied, for scripts/render_kinect_steady_demo.py to draw side by side.
#include "kinect_stability.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>

namespace {
namespace joint = sfr::nui_joint;
constexpr double rate = 30.0;

struct Scene {
    const char* name;
    double seconds;
    bool menu;
};
constexpr Scene scenes[] = {
    {"arms crossed in a race", 4.0, false},
    {"right hand behind the back", 3.5, false},
    {"fast punch (must not lag)", 2.0, false},
    {"menu: hand held on a button", 4.0, true},
    {"menu: slow move, then hold", 3.5, true},
};

sfr::KinectBody standing() {
    sfr::KinectBody body;
    body.tracking_id = 5;
    const auto set = [&](uint32_t j, float x, float y) {
        body.joints[j] = {x, y, 2.4f};
        body.joint_states[j] = 2;
    };
    set(joint::hip_center, 0, 0);       set(joint::spine, 0, 0.2f);
    set(joint::shoulder_center, 0, 0.5f); set(joint::head, 0, 0.72f);
    set(joint::shoulder_left, -0.18f, 0.5f);  set(joint::shoulder_right, 0.18f, 0.5f);
    set(joint::elbow_left, -0.2f, 0.22f);     set(joint::elbow_right, 0.2f, 0.22f);
    set(joint::wrist_left, -0.21f, -0.03f);   set(joint::wrist_right, 0.21f, -0.03f);
    set(joint::hand_left, -0.21f, -0.1f);     set(joint::hand_right, 0.21f, -0.1f);
    set(joint::hip_left, -0.1f, 0);   set(joint::hip_right, 0.1f, 0);
    set(joint::knee_left, -0.1f, -0.42f);  set(joint::knee_right, 0.1f, -0.42f);
    set(joint::ankle_left, -0.1f, -0.82f); set(joint::ankle_right, 0.1f, -0.82f);
    set(joint::foot_left, -0.12f, -0.88f); set(joint::foot_right, 0.12f, -0.88f);
    return body;
}

void place_arm(sfr::KinectBody& b, bool right, float ex, float ey, float ez, float wx, float wy, float wz) {
    const uint32_t e = right ? joint::elbow_right : joint::elbow_left;
    const uint32_t w = right ? joint::wrist_right : joint::wrist_left;
    const uint32_t h = right ? joint::hand_right : joint::hand_left;
    b.joints[e] = {ex, ey, ez};
    b.joints[w] = {wx, wy, wz};
    // The hand a little beyond the wrist, along the forearm.
    b.joints[h] = {wx + (wx - ex) * 0.27f, wy + (wy - ey) * 0.27f, wz + (wz - ez) * 0.27f};
}

void print(const char* scene, bool menu, int frame, const sfr::KinectBody& given, const sfr::KinectBody& out) {
    std::printf("{\"frame\":%d,\"scene\":\"%s\",\"menu\":%d,\"given\":[", frame, scene, menu ? 1 : 0);
    for (uint32_t j = 0; j < sfr::nui_joint_count; ++j)
        std::printf("%s[%.4f,%.4f,%.4f,%u]", j ? "," : "", given.joints[j][0], given.joints[j][1], given.joints[j][2],
                    given.joint_states[j]);
    std::printf("],\"out\":[");
    for (uint32_t j = 0; j < sfr::nui_joint_count; ++j)
        std::printf("%s[%.4f,%.4f,%.4f]", j ? "," : "", out.joints[j][0], out.joints[j][1], out.joints[j][2]);
    std::printf("]}\n");
}
}

int main() {
    std::mt19937 random(7);
    std::normal_distribution<float> tremble(0.0f, 0.0025f);  // a still hand, metres
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    sfr::KinectBodySteadying steadying(true, true);
    int frame = 0;
    for (const Scene& scene : scenes) {
        const int frames = int(scene.seconds * rate);
        for (int i = 0; i < frames; ++i, ++frame) {
            const float t = float(i) / float(frames);
            sfr::KinectBody b = standing();
            const std::string name = scene.name;
            if (name.rfind("arms crossed", 0) == 0) {
                // The arms fold across the chest and open again; while crossed
                // the sensor only guesses the hidden right forearm.
                const float fold = std::fmin(1.0f, std::fmax(0.0f, std::sin(t * 3.14159f) * 1.6f));
                place_arm(b, false, -0.2f + 0.08f * fold, 0.22f + 0.05f * fold, 2.4f - 0.15f * fold,
                          -0.21f + 0.36f * fold, -0.03f + 0.36f * fold, 2.4f - 0.3f * fold);
                place_arm(b, true, 0.2f - 0.08f * fold, 0.22f + 0.05f * fold, 2.4f - 0.12f * fold,
                          0.21f - 0.36f * fold, -0.03f + 0.32f * fold, 2.4f - 0.25f * fold);
                if (fold > 0.6f) {
                    for (uint32_t j : {joint::wrist_right, joint::hand_right}) b.joint_states[j] = 1;
                    const float r = unit(random);
                    if (r < 0.35f) {
                        // Guessed on the other side of the body, the arm folded wrong.
                        b.joints[joint::wrist_right] = {0.35f, 0.05f, 2.35f};
                        b.joints[joint::hand_right] = {0.42f, -0.02f, 2.35f};
                    } else if (r < 0.6f) {
                        b.joints[joint::wrist_right][1] -= 0.4f;  // dropped half a metre
                        b.joints[joint::hand_right][1] -= 0.45f;
                    }
                }
            } else if (name.rfind("right hand behind", 0) == 0) {
                // The right hand goes behind the back: inferred, and guessed
                // anywhere around the hip, often in front of the body.
                const float back = std::fmin(1.0f, std::fmax(0.0f, std::sin(t * 3.14159f) * 1.5f));
                place_arm(b, true, 0.22f, 0.24f, 2.4f + 0.1f * back, 0.22f - 0.1f * back, 0.0f, 2.4f + 0.2f * back);
                if (back > 0.5f) {
                    for (uint32_t j : {joint::wrist_right, joint::hand_right}) b.joint_states[j] = 1;
                    if (unit(random) < 0.5f) {
                        const float swing = unit(random);
                        b.joints[joint::wrist_right] = {-0.05f + 0.35f * swing, 0.1f - 0.3f * swing, 2.2f};
                        b.joints[joint::hand_right] = {-0.08f + 0.4f * swing, 0.05f - 0.35f * swing, 2.2f};
                    }
                }
            } else if (name.rfind("fast punch", 0) == 0) {
                // Three straight right punches at the sensor: all tracked, all real.
                const float phase = std::fmod(t * 3.0f, 1.0f);
                const float reach = phase < 0.3f ? phase / 0.3f : phase < 0.5f ? 1.0f : 1.0f - (phase - 0.5f) / 0.5f;
                place_arm(b, true, 0.2f + 0.02f * reach, 0.25f + 0.15f * reach, 2.4f - 0.3f * reach,
                          0.18f, 0.2f + 0.25f * reach, 2.3f - 0.55f * reach);
            } else {
                // The right hand raised as the menu cursor. Held still: a few
                // millimetres of wandering, and now and then one frame half a
                // metre off. The slow scene moves 15 cm over a second first.
                float hx = 0.32f, hy = 0.62f;
                if (name.rfind("menu: slow", 0) == 0 && t < 0.3f) hx -= 0.15f * (t / 0.3f);
                else if (name.rfind("menu: slow", 0) == 0) hx -= 0.15f;
                hx += tremble(random);
                hy += tremble(random);
                if (unit(random) < 0.03f) hx += 0.45f;
                place_arm(b, true, 0.27f, 0.42f, 2.25f, hx, hy - 0.05f, 2.05f);
                b.joints[joint::hand_right] = {hx, hy, 2.0f};
            }
            sfr::KinectBody out = b;
            std::vector<sfr::KinectBody> bodies{out};
            steadying.steady(bodies, 1.0 / rate, scene.menu);
            print(scene.name, scene.menu, frame, b, bodies[0]);
        }
    }
    return 0;
}
