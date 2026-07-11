#include "scene/AnimationClip.hpp"

// TODO(evening): port Ogre AnimationTrack::getInterpolatedKeyFrame — binary
// search the key pair around t, lerp pos/scale, slerp rot (tmp/OgreAnimationTrack.cpp).

void AnimationClip::sample(float t, LocalPose* out) const
{
    (void)t;
    (void)out; // TODO(evening)
}
