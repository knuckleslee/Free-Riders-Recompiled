#include "kinect_depth_layer.h"
#include "kinect_preview.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sfr {
namespace {
constexpr uint32_t width = 320, height = 240;
// NUI_CAMERA_DEPTH_NOMINAL_FOCAL_LENGTH_IN_PIXELS at 320x240, as
// kinect_depth_point uses: a metre at distance z spans this / z pixels.
constexpr float focal = 285.63f;

// One pixel: the surface's distance in metres (0 for none) and its player.
struct Pixel {
    float metres = 0;
    uint32_t player = 0;
};
Pixel pixel_at(const KinectImage& depth, int x, int y) {
    if (x < 0 || y < 0 || x >= int(width) || y >= int(height)) return {};
    uint16_t value;
    std::memcpy(&value, depth.pixels.data() + (size_t(y) * width + size_t(x)) * 2, 2);
    const uint32_t millimetres = value >> 3;
    // 0 is no reading (a shadow, a shiny or dark surface, nearer than the
    // sensor can see); 0x0fff and 0x1fff are the SDK's "too far" and
    // "unknown" markers. None of them is a surface at a known distance.
    if (millimetres == 0 || millimetres == 0x0fff || millimetres == 0x1fff) return {};
    return {float(millimetres) * 0.001f, value & 7u};
}

// What one pixel says about a point at depth z, for player index `player`.
enum class Sample : uint8_t { none, agree, other_agree, front, behind_own, behind_other, between };
Sample sample(const KinectImage& depth, int x, int y, float z, uint32_t player) {
    const Pixel p = pixel_at(depth, x, y);
    if (p.metres <= 0) return Sample::none;
    const float gap = p.metres - z;  // positive: the surface is beyond the point
    if (gap >= -kinect_depth_agree_before && gap <= kinect_depth_agree_beyond)
        return p.player == player ? Sample::agree : Sample::other_agree;
    if (gap < -kinect_depth_clear_margin) return Sample::front;
    if (gap > kinect_depth_clear_margin) return p.player == player ? Sample::behind_own : Sample::behind_other;
    return Sample::between;
}

// Where the samples go around a point: a 3x3 grid about 1.5 cm apart (on a
// wrist or a hand, which are 5 to 10 cm across, all nine fall on it), and a
// ring of eight 6 cm out. The ring is only asked whether anything of the
// player is near at the point's depth: a hand moving fast is a few
// centimetres from where an image a frame older shows it, and is then not
// called missing.
struct Pattern {
    int x = 0, y = 0, step = 1, ring = 2;
};
bool pattern_for(const std::array<float, 3>& point, Pattern& pattern) {
    if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2])) return false;
    const KinectImagePoint at = kinect_depth_point(point);
    if (!at.visible) return false;
    const float x = std::floor(at.x), y = std::floor(at.y);
    if (!(x >= 0 && y >= 0 && x < float(width) && y < float(height))) return false;
    pattern.x = int(x);
    pattern.y = int(y);
    const float per_metre = focal / point[2];
    pattern.step = std::clamp(int(std::lround(0.015f * per_metre)), 1, 4);
    pattern.ring = std::max(2, int(std::lround(0.06f * per_metre)));
    return true;
}
constexpr std::array<std::array<float, 2>, 8> ring_directions{{
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7071f, 0.7071f}, {-0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, -0.7071f},
}};

struct Tally {
    uint32_t valid = 0, agree = 0, front = 0, behind_own = 0, behind_other = 0;
    void add(Sample s) {
        if (s == Sample::none) return;
        ++valid;
        agree += s == Sample::agree;
        front += s == Sample::front;
        behind_own += s == Sample::behind_own;
        behind_other += s == Sample::behind_other;
    }
    uint32_t behind() const { return behind_own + behind_other; }
};
}

const char* kinect_depth_verdict_name(KinectDepthVerdict verdict) {
    switch (verdict) {
    case KinectDepthVerdict::seen: return "seen";
    case KinectDepthVerdict::hidden: return "hidden";
    case KinectDepthVerdict::impossible: return "impossible";
    case KinectDepthVerdict::elsewhere: return "elsewhere";
    default: return "unknown";
    }
}

bool kinect_depth_image_usable(const KinectImage& depth) {
    return depth.number && depth.width == width && depth.height == height && depth.bytes_per_pixel == 2 &&
           depth.pixels.size() >= size_t(width) * height * 2;
}

KinectDepthVerdict kinect_depth_verdict(const KinectImage& depth, uint32_t player, const std::array<float, 3>& point) {
    Pattern pattern;
    if (player < 1 || player > 6 || !kinect_depth_image_usable(depth) || !pattern_for(point, pattern))
        return KinectDepthVerdict::unknown;
    const float z = point[2];
    Tally centre, all;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const Sample s = sample(depth, pattern.x + dx * pattern.step, pattern.y + dy * pattern.step, z, player);
            centre.add(s);
            all.add(s);
        }
    for (const auto& d : ring_directions)
        all.add(sample(depth, pattern.x + int(std::lround(d[0] * float(pattern.ring))),
                       pattern.y + int(std::lround(d[1] * float(pattern.ring))), z, player));

    // Too many holes where the point is: the sensor did not see that spot.
    if (centre.valid < 4) return KinectDepthVerdict::unknown;
    // A third of the grid on the player at the point's depth is the limb
    // itself (a point on a limb's edge has half its grid off the limb).
    if (centre.agree >= 3) return KinectDepthVerdict::seen;
    // Most of the grid covered by something nearer: behind it, unseen.
    if (centre.front * 2 >= centre.valid) return KinectDepthVerdict::hidden;
    // Nothing of the player at the point's depth anywhere near it, and the
    // line of sight clearly passing it nearly everywhere: nothing is there.
    // Any doubt (a sample at its depth, a mixture) leaves it unknown.
    if (all.agree == 0 && centre.front == 0 && centre.behind() * 3 >= centre.valid * 2 && all.behind() * 3 >= all.valid * 2)
        return centre.behind_own * 2 > centre.behind() ? KinectDepthVerdict::impossible : KinectDepthVerdict::elsewhere;
    return KinectDepthVerdict::unknown;
}

bool kinect_depth_matches_body(const KinectImage& depth, const KinectBody& body) {
    if (!kinect_depth_image_usable(depth) || body.sensor_index > 5) return false;
    const uint32_t player = body.sensor_index + 1;
    // The trunk's joints lie deeper in the body than a limb's: the spine is
    // a hand's breadth behind the chest. Its own pixels anywhere from 30 cm
    // in front of it to a little behind it are the trunk.
    uint32_t matched = 0;
    for (uint32_t joint : {nui_joint::spine, nui_joint::shoulder_center, nui_joint::hip_center}) {
        if (body.joint_states[joint] < 2) continue;
        const auto& point = body.joints[joint];
        Pattern pattern;
        if (!pattern_for(point, pattern)) continue;
        uint32_t own = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Pixel p = pixel_at(depth, pattern.x + dx * pattern.step, pattern.y + dy * pattern.step);
                const float gap = p.metres - point[2];
                own += p.metres > 0 && p.player == player && gap >= -0.30f && gap <= kinect_depth_clear_margin;
            }
        matched += own >= 5;
    }
    return matched >= 2;
}

KinectDepthLayers kinect_depth_layers(const KinectImage& depth, const KinectBody& body) {
    KinectDepthLayers layers;
    if (!kinect_depth_matches_body(depth, body)) return layers;
    layers.checked = true;
    for (uint32_t joint : kinect_depth_points)
        layers.verdicts[joint] = kinect_depth_verdict(depth, body.sensor_index + 1, body.joints[joint]);
    return layers;
}

void KinectDepthCounts::add(KinectDepthVerdict verdict) {
    switch (verdict) {
    case KinectDepthVerdict::seen: ++seen; break;
    case KinectDepthVerdict::hidden: ++hidden; break;
    case KinectDepthVerdict::impossible: ++impossible; break;
    case KinectDepthVerdict::elsewhere: ++elsewhere; break;
    default: ++unknown; break;
    }
}

KinectDepthCounts& KinectDepthCounts::operator+=(const KinectDepthCounts& other) {
    seen += other.seen;
    hidden += other.hidden;
    impossible += other.impossible;
    elsewhere += other.elsewhere;
    unknown += other.unknown;
    raised += other.raised;
    lowered += other.lowered;
    unmatched += other.unmatched;
    return *this;
}

}
