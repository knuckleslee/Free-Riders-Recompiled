#pragma once
#include "kinect_sensor.h"

#include <array>
#include <cstdint>

namespace sfr {

// The sensor's skeleton checked against the depth image it was made from.
//
// When a limb is hidden the SDK still names a place for it, marked inferred,
// and nothing in the skeleton says whether that place is believable. The
// depth image can: every pixel says how far away the nearest surface is
// along that line of sight, and which tracked player (if any) it belongs to.
// So each limb point (elbows, wrists, hands, knees, ankles) is projected into
// the depth image, exactly as the SDK does it (kinect_depth_point), and what
// the sensor saw along that line is compared with how far away the skeleton
// says the point is:
//   - the line ends on this player's own body at about the point's depth:
//     the point is there, in plain view (seen);
//   - the line ends on something clearly nearer (the player's other arm, the
//     torso, another player): the point may well be behind it, and the image
//     cannot say where (hidden);
//   - the line passes the point's depth and ends clearly farther away: there
//     is nothing where the skeleton puts the point, or it would have stopped
//     the line. If what it ends on is this player's own body (a hand guessed
//     in front of the chest while the chest is what the sensor sees), the
//     place is impossible; if it is the wall or someone else, the point is in
//     the air or on another player (elsewhere);
//   - off the image, no depth there, or a mixture that says neither (unknown).
// No learning and no model: a few pixel reads a point, and the geometry of a
// line of sight.
//
// A skeleton joint is the middle of a limb, not its skin: the surface the
// sensor measures is a few centimetres nearer (half a wrist, half a hand,
// less than half an elbow). The sensor's own depth steps are about 1.5 cm at
// 2.5 m and 4.5 cm at 4 m, and the image may be a frame older than the
// skeleton. So a surface from 7 cm nearer than the point to 5 cm beyond it
// counts as the point, and only one more than 15 cm nearer or farther counts
// as clearly in front or behind; between the two says nothing. The window is
// kept narrow on the near side on purpose: the trunk's front is about 10 cm
// before the depth of the hips and shoulders, so a hand guessed beside the
// body, at the body's depth, does not pass for seen on the trunk's pixels.
// A hand really within a few centimetres of the chest cannot be told from
// the chest, and is seen; that is the one place where the image cannot
// overrule the sensor.
//
// Before any limb is judged the body's trunk must be where the image says it
// is: a depth image that does not line up with the skeleton (a different
// resolution, a stale frame from before the player moved, a player index
// that does not match the skeleton's slot) would make every hand look out
// of place, and is not used at all rather than trusted.
enum class KinectDepthVerdict : uint8_t { unknown, seen, hidden, impossible, elsewhere };
const char* kinect_depth_verdict_name(KinectDepthVerdict verdict);

struct KinectDepthLayers {
    // The trunk matched the image; when false every verdict is unknown.
    bool checked = false;
    // nui_joint order; joints that are not limb points stay unknown.
    std::array<KinectDepthVerdict, nui_joint_count> verdicts{};
};

// The depth and player image as the SDK gives it at 320x240, which is the
// resolution kinect_depth_point maps to; any other is not checked.
bool kinect_depth_image_usable(const KinectImage& depth);
// One point of space against the image, for the player whose pixels carry
// this player index (sensor_index + 1).
KinectDepthVerdict kinect_depth_verdict(const KinectImage& depth, uint32_t player_index, const std::array<float, 3>& point);
// Whether the body's trunk (spine, shoulder centre, hip centre) lies on its
// own pixels in the image.
bool kinect_depth_matches_body(const KinectImage& depth, const KinectBody& body);
// Every limb point of one body.
KinectDepthLayers kinect_depth_layers(const KinectImage& depth, const KinectBody& body);

// The limb points judged, for the log.
constexpr std::array<uint32_t, 10> kinect_depth_points{
    nui_joint::elbow_left, nui_joint::wrist_left, nui_joint::hand_left,
    nui_joint::elbow_right, nui_joint::wrist_right, nui_joint::hand_right,
    nui_joint::knee_left, nui_joint::ankle_left, nui_joint::knee_right, nui_joint::ankle_right,
};

// Metres: a surface from agree_before nearer than a point to agree_beyond
// farther is the point; one more than clear_margin in front of it or behind
// it is something else.
constexpr float kinect_depth_agree_before = 0.07f, kinect_depth_agree_beyond = 0.05f;
constexpr float kinect_depth_clear_margin = 0.15f;

// What the checks found, over a frame or since the last log line.
struct KinectDepthCounts {
    uint32_t seen = 0, hidden = 0, impossible = 0, elsewhere = 0, unknown = 0;
    // Limb points whose confidence the verdict changed: an inferred point
    // believed because it is seen, a tracked one doubted because it is not
    // where the image says anything is.
    uint32_t raised = 0, lowered = 0;
    // Bodies whose trunk did not match the image, so were not checked.
    uint32_t unmatched = 0;
    void add(KinectDepthVerdict verdict);
    KinectDepthCounts& operator+=(const KinectDepthCounts& other);
};

}
