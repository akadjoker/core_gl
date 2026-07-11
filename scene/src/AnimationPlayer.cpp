#include "scene/AnimationPlayer.hpp"
#include "scene/Skeleton.hpp"

// TODO(evening): port Ogre AnimationState weight/time semantics + linear
// weighted blending (tmp/OgreAnimationState.cpp), Sinbad-style layer masks.

void AnimationLayer::play(const std::string& clip, PlayMode mode, float blendTime)
{
    (void)clip; (void)mode; (void)blendTime; // TODO(evening)
}
void AnimationLayer::cross_fade(const std::string& clip, float duration)
{
    (void)clip; (void)duration; // TODO(evening)
}
void AnimationLayer::play_one_shot(const std::string& clip, const std::string& returnTo,
                                   float blendIn)
{
    (void)clip; (void)returnTo; (void)blendIn; // TODO(evening)
}
void AnimationLayer::stop(float blendOut) { (void)blendOut; }
void AnimationLayer::mask_from_bone(const Skeleton& skel, const char* rootBone, float weight)
{
    (void)skel; (void)rootBone; (void)weight; // TODO(evening): subtree fill
}
void AnimationLayer::mask_all(const Skeleton& skel, float weight)
{
    m_mask.assign((size_t)skel.bone_count(), weight);
}
bool AnimationLayer::is_playing(const std::string& clip) const { return m_currentName == clip; }
float AnimationLayer::normalized_time() const { return 0.f; } // TODO(evening)
bool AnimationLayer::finished() const { return false; }       // TODO(evening)

void AnimationPlayer::bind(const Skeleton* skeleton, const std::vector<AnimationClip*>* clips)
{
    m_skeleton = skeleton;
    m_clips = clips;
}
AnimationLayer& AnimationPlayer::layer(int i)
{
    if ((int)m_layers.size() <= i) m_layers.resize((size_t)i + 1);
    return m_layers[(size_t)i];
}
void AnimationPlayer::play(const std::string& clip, PlayMode mode, float blendTime)
{
    layer(0).play(clip, mode, blendTime);
}
void AnimationPlayer::update(float dt, LocalPose* out)
{
    (void)dt;
    (void)out; // TODO(evening): advance layers, blend bind->0->1->...
}
const AnimationClip* AnimationPlayer::find_clip(const std::string& name) const
{
    if (!m_clips) return nullptr;
    for (AnimationClip* c : *m_clips)
        if (c && c->name() == name) return c;
    return nullptr;
}
