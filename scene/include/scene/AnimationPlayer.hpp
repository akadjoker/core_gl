#pragma once

#include "scene/AnimationClip.hpp"
#include <string>
#include <vector>

class Skeleton;

enum class PlayMode : gl::u8
{
    Loop,
    Once,
    PingPong
};

// One animation layer — a body part's playback slot (Ogre/Sinbad style:
// layer 0 = legs with a lower-body mask playing Run, layer 1 = torso with
// an upper-body mask playing Wave). Each layer plays/crossfades one clip
// at a time; the player blends layers in order into the final pose.
class AnimationLayer
{
public:
    // playback (API kept from the old Animator — it was the right shape)
    void play(const std::string& clip, PlayMode mode = PlayMode::Loop,
              float blendTime = 0.2f);
    void cross_fade(const std::string& clip, float duration = 0.2f);
    // plays `clip` once, then blends back into `returnTo`
    void play_one_shot(const std::string& clip, const std::string& returnTo,
                       float blendIn = 0.2f);
    void stop(float blendOut = 0.2f);
    void set_speed(float s) { m_speed = s; }

    // bone mask: weight per bone (0 = untouched by this layer). Helpers
    // set a bone and optionally its whole subtree (mask_from_bone("Spine")).
    void set_mask(const std::vector<float>& weights) { m_mask = weights; }
    void mask_from_bone(const Skeleton& skel, const char* rootBone, float weight = 1.f);
    void mask_all(const Skeleton& skel, float weight);

    // queries
    bool is_playing(const std::string& clip) const;
    const std::string& current() const { return m_currentName; }
    float normalized_time() const;
    bool finished() const; // Once mode reached the end

private:
    friend class AnimationPlayer;
    // internals rebuilt tonight from Ogre AnimationState semantics:
    // current + previous clip during a crossfade, each with time/weight
    const AnimationClip* m_current = nullptr;
    const AnimationClip* m_previous = nullptr;
    std::string m_currentName;
    std::string m_returnTo; // one-shot: clip to go back to
    float m_time = 0.f, m_prevTime = 0.f;
    float m_blend = 1.f, m_blendDur = 0.f; // 0..1 previous->current
    float m_speed = 1.f;
    PlayMode m_mode = PlayMode::Loop;
    std::vector<float> m_mask; // empty = whole body
};

// Per-instance animation state machine (Ogre's AnimationStateSet): owns the
// layers, resolves clip names against the shared SkinnedMesh's clips, and
// produces the blended LocalPose buffer each frame. One per
// SkinnedMeshInstance — five characters sharing one mesh each have their own
// player, times and layers.
class AnimationPlayer
{
public:
    // clips are owned by the shared SkinnedMesh; the player only refers
    void bind(const Skeleton* skeleton, const std::vector<AnimationClip*>* clips);

    // layers are created on demand and blended in index order
    AnimationLayer& layer(int i);
    int layer_count() const { return (int)m_layers.size(); }

    // convenience for the common single-layer case → layer(0)
    void play(const std::string& clip, PlayMode mode = PlayMode::Loop,
              float blendTime = 0.2f);

    // advances every layer and blends them (bind pose -> layer 0 -> layer 1
    // ...) into `out` (bone_count() long). Ogre linear weighted blending.
    void update(float dt, LocalPose* out);

    const AnimationClip* find_clip(const std::string& name) const;

private:
    const Skeleton* m_skeleton = nullptr;
    const std::vector<AnimationClip*>* m_clips = nullptr;
    std::vector<AnimationLayer> m_layers;
    std::vector<LocalPose> m_scratch; // per-layer sample buffer, reused
};
