#pragma once
// Made-up depth and player images for the depth check's tests and demo
// (kinect_depth_layer.h): a 320x240 image as the Kinect v1 gives it, a wall
// behind, and bodies drawn into it as boxes and rounded limbs, each surface
// as far away as the skeleton puts it, the nearest one kept per pixel.
#include "kinect_preview.h"
#include "kinect_sensor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <tuple>

namespace sfr_test {

struct DepthScene {
    static constexpr int width = 320, height = 240;
    static constexpr float focal = 285.63f;
    sfr::KinectImage image;

    // wall_metres 0 leaves the background without depth (out of range).
    explicit DepthScene(float wall_metres = 3.5f) {
        image.number = 1;
        image.width = width;
        image.height = height;
        image.bytes_per_pixel = 2;
        image.pixels.assign(size_t(width) * height * 2, 0);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) set(x, y, wall_metres, 0);
    }
    void set(int x, int y, float metres, uint32_t player) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const uint16_t value = uint16_t((uint32_t(std::lround(metres * 1000.0f)) << 3) | (player & 7u));
        std::memcpy(image.pixels.data() + (size_t(y) * width + size_t(x)) * 2, &value, 2);
    }
    float metres(int x, int y) const {
        uint16_t value;
        std::memcpy(&value, image.pixels.data() + (size_t(y) * width + size_t(x)) * 2, 2);
        return float(value >> 3) * 0.001f;
    }
    // Keeps the nearer of what is there and this surface.
    void put(int x, int y, float surface, uint32_t player) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const float there = metres(x, y);
        if (there > 0 && there <= surface) return;
        set(x, y, surface, player);
    }
    // A flat box facing the sensor, its corners in metres at depth z.
    void box(float x0, float y0, float x1, float y1, float z, uint32_t player) {
        const auto a = sfr::kinect_depth_point({x0, y1, z}), b = sfr::kinect_depth_point({x1, y0, z});
        for (int y = int(std::floor(a.y)); y <= int(std::floor(b.y)); ++y)
            for (int x = int(std::floor(a.x)); x <= int(std::floor(b.x)); ++x) put(x, y, z, player);
    }
    // A ball: its front surface radius nearer than its centre.
    void ball(const std::array<float, 3>& c, float radius, uint32_t player) {
        const auto at = sfr::kinect_depth_point(c);
        if (!at.visible) return;
        const float r = radius * focal / c[2];
        for (int y = int(std::floor(at.y - r)); y <= int(std::ceil(at.y + r)); ++y)
            for (int x = int(std::floor(at.x - r)); x <= int(std::ceil(at.x + r)); ++x) {
                const float dx = (float(x) + 0.5f - at.x) / r, dy = (float(y) + 0.5f - at.y) / r;
                const float d2 = dx * dx + dy * dy;
                if (d2 <= 1.0f) put(x, y, c[2] - radius * std::sqrt(1.0f - d2), player);
            }
    }
    // A limb from a to b, as balls along it.
    void limb(const std::array<float, 3>& a, const std::array<float, 3>& b, float radius, uint32_t player) {
        for (int i = 0; i <= 24; ++i) {
            const float t = float(i) / 24.0f;
            ball({a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t}, radius, player);
        }
    }
    // A whole body where its skeleton says it is: the trunk a box from the
    // hips to just above the shoulders, its front 8 cm before the spine;
    // head, arms and legs as rounded limbs. This is the body as it really
    // stands; the skeleton a test hands on may guess some of it elsewhere.
    void body(const sfr::KinectBody& b) {
        namespace j = sfr::nui_joint;
        const uint32_t player = b.sensor_index + 1;
        const auto& s = b.joints;
        const float trunk_z = s[j::spine][2] - 0.08f;
        box(s[j::shoulder_left][0] - 0.02f, s[j::hip_center][1] - 0.05f, s[j::shoulder_right][0] + 0.02f,
            s[j::shoulder_center][1] + 0.05f, trunk_z, player);
        ball(s[j::head], 0.1f, player);
        for (auto [a, c, r] : {std::tuple{j::shoulder_left, j::elbow_left, 0.05f}, {j::elbow_left, j::wrist_left, 0.035f},
                               {j::wrist_left, j::hand_left, 0.045f}, {j::shoulder_right, j::elbow_right, 0.05f},
                               {j::elbow_right, j::wrist_right, 0.035f}, {j::wrist_right, j::hand_right, 0.045f},
                               {j::hip_left, j::knee_left, 0.07f}, {j::knee_left, j::ankle_left, 0.05f},
                               {j::ankle_left, j::foot_left, 0.04f}, {j::hip_right, j::knee_right, 0.07f},
                               {j::knee_right, j::ankle_right, 0.05f}, {j::ankle_right, j::foot_right, 0.04f}})
            limb(s[a], s[c], r, player);
    }
};

}
