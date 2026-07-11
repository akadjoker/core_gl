#pragma once

#include "scene/Skeleton.hpp"
#include <string>
#include <vector>

// One bone's keyframe track. Keyframes are sorted by time; sampling uses
// Ogre's scheme (AnimationTrack::getInterpolatedKeyFrame): find the pair
// around t, lerp position/scale, slerp rotation.
struct BoneTrack
{
    gl::i32 bone = -1; // index into the skeleton (bound by name at load)
    std::vector<float> times;
    std::vector<Vec3> positions;      // one per key (empty = bind value)
    std::vector<Quaternion> rotations;
    std::vector<Vec3> scales;
};

// A shareable animation ("Run", "Wave"): tracks for the bones it animates.
// Bones without a track keep whatever pose the caller left in the buffer
// (the bind pose, or a lower layer's result) — that's what makes per-part
// layering work.
class AnimationClip
{
public:
    const std::string& name() const { return m_name; }
    float duration() const { return m_duration; }

    // samples every track at time t (seconds, caller wraps/clamps) into
    // `out` (skeleton bone_count() long)
    void sample(float t, LocalPose* out) const;

    // building (the .anim loader fills these)
    void set_name(const std::string& n) { m_name = n; }
    void set_duration(float d) { m_duration = d; }
    std::vector<BoneTrack>& tracks() { return m_tracks; }
    const std::vector<BoneTrack>& tracks() const { return m_tracks; }

private:
    std::string m_name;
    float m_duration = 0.f;
    std::vector<BoneTrack> m_tracks;
};
