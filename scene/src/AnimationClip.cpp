#include "scene/AnimationClip.hpp"
#include <algorithm>

// Ogre AnimationTrack::getInterpolatedKeyFrame: find the key pair around t,
// lerp position/scale, normalized-lerp rotation (Ogre's default rotation
// interpolation is IM_LINEAR = nlerp; cheap and artifact-free for the short
// spans between authored keys).
void AnimationClip::sample(float t, LocalPose* out) const
{
    for (const BoneTrack& tr : m_tracks)
    {
        if (tr.bone < 0 || tr.times.empty()) continue;
        LocalPose& p = out[(size_t)tr.bone];

        if (tr.times.size() == 1 || t <= tr.times.front())
        {
            p.position = tr.positions.front();
            p.rotation = tr.rotations.front();
            p.scale = tr.scales.front();
            continue;
        }
        if (t >= tr.times.back())
        {
            p.position = tr.positions.back();
            p.rotation = tr.rotations.back();
            p.scale = tr.scales.back();
            continue;
        }

        // first key with time > t; k0 is the one before it
        const size_t k1 =
            (size_t)(std::upper_bound(tr.times.begin(), tr.times.end(), t) - tr.times.begin());
        const size_t k0 = k1 - 1;
        const float span = tr.times[k1] - tr.times[k0];
        const float f = span > 1e-6f ? (t - tr.times[k0]) / span : 0.f;

        p.position = tr.positions[k0] + (tr.positions[k1] - tr.positions[k0]) * f;
        p.rotation = Quaternion::Nlerp(tr.rotations[k0], tr.rotations[k1], f);
        p.scale = tr.scales[k0] + (tr.scales[k1] - tr.scales[k0]) * f;
    }
}
