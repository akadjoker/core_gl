#include "scene/AnimationPlayer.hpp"
#include "scene/Skeleton.hpp"
#include <cmath>

// ── AnimationLayer ──────────────────────────────────────────────────────

void AnimationLayer::play(const std::string& clip, PlayMode mode, float blendTime)
{
    if (m_currentName == clip && m_mode == mode) return;
    m_previous = m_current;
    m_prevTime = m_time;
    m_prevMode = m_mode;
    m_current = nullptr; // player resolves by name on next update
    m_currentName = clip;
    m_returnTo.clear();
    m_time = 0.f;
    m_mode = mode;
    m_blendDur = blendTime;
    m_blend = blendTime > 0.f ? 0.f : 1.f;
}

void AnimationLayer::cross_fade(const std::string& clip, float duration)
{
    play(clip, m_mode, duration);
}

void AnimationLayer::play_one_shot(const std::string& clip, const std::string& returnTo,
                                   float blendIn)
{
    play(clip, PlayMode::Once, blendIn);
    m_returnTo = returnTo;
}

void AnimationLayer::stop(float blendOut)
{
    (void)blendOut; // v1: immediate; layers with empty current are skipped
    m_current = nullptr;
    m_previous = nullptr;
    m_currentName.clear();
    m_returnTo.clear();
}

void AnimationLayer::mask_all(const Skeleton& skel, float weight)
{
    m_mask.assign((size_t)skel.bone_count(), weight);
}

// weight for `rootBone` and every descendant — the Sinbad split: layer 0
// mask_from_bone("Root")=1 then legs full, layer 1 mask_from_bone("Spine")
void AnimationLayer::mask_from_bone(const Skeleton& skel, const char* rootBone, float weight)
{
    const int root = skel.find_bone(rootBone);
    if (root < 0) return;
    if (m_mask.size() != (size_t)skel.bone_count())
        m_mask.assign((size_t)skel.bone_count(), 0.f);
    for (int i = 0; i < skel.bone_count(); ++i)
    {
        int b = i;
        while (b >= 0)
        {
            if (b == root)
            {
                m_mask[(size_t)i] = weight;
                break;
            }
            b = skel.bone(b).parent;
        }
    }
}

bool AnimationLayer::is_playing(const std::string& clip) const { return m_currentName == clip; }

float AnimationLayer::normalized_time() const
{
    return (m_current && m_current->duration() > 0.f) ? m_time / m_current->duration() : 0.f;
}

bool AnimationLayer::finished() const
{
    return m_current && m_mode == PlayMode::Once && m_time >= m_current->duration();
}

// ── AnimationPlayer ─────────────────────────────────────────────────────

void AnimationPlayer::bind(const Skeleton* skeleton, const std::vector<AnimationClip*>* clips)
{
    m_skeleton = skeleton;
    m_clips = clips;
    m_scratch.resize(skeleton ? (size_t)skeleton->bone_count() : 0);
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

const AnimationClip* AnimationPlayer::find_clip(const std::string& name) const
{
    if (!m_clips) return nullptr;
    for (AnimationClip* c : *m_clips)
        if (c && c->name() == name) return c;
    return nullptr;
}

static float wrapTime(float t, float duration, PlayMode mode)
{
    if (duration <= 0.f) return 0.f;
    switch (mode)
    {
    case PlayMode::Loop:
        t = fmodf(t, duration);
        return t < 0.f ? t + duration : t;
    case PlayMode::Once:
        return t > duration ? duration : t;
    case PlayMode::PingPong:
    {
        float c = fmodf(t, duration * 2.f);
        if (c < 0.f) c += duration * 2.f;
        return c <= duration ? c : duration * 2.f - c;
    }
    }
    return t;
}

// Ogre's ANIMBLEND_LINEAR apply: sample the clip, then per bone move the
// accumulated pose toward the sample by weight (lerp pos/scale, nlerp rot).
// The mask scales the weight per bone; scratch starts as a copy of the
// accumulated pose so untracked bones blend as no-ops.
static void applyClip(const AnimationClip* clip, float t, float weight,
                      const std::vector<float>& mask, LocalPose* out, LocalPose* scratch,
                      int boneCount)
{
    if (!clip || weight <= 0.001f) return;
    for (int i = 0; i < boneCount; ++i)
        scratch[i] = out[i];
    clip->sample(t, scratch);
    for (int i = 0; i < boneCount; ++i)
    {
        const float w = weight * (mask.empty() ? 1.f : mask[(size_t)i]);
        if (w <= 0.001f) continue;
        LocalPose& d = out[i];
        const LocalPose& s = scratch[i];
        d.position = d.position + (s.position - d.position) * w;
        d.rotation = Quaternion::Nlerp(d.rotation, s.rotation, w);
        d.scale = d.scale + (s.scale - d.scale) * w;
    }
}

void AnimationPlayer::update(float dt, LocalPose* out)
{
    if (!m_skeleton || m_skeleton->empty()) return;
    const int n = m_skeleton->bone_count();

    // base: the bind pose — layers blend over it in index order
    m_skeleton->bind_pose(out);

    for (AnimationLayer& L : m_layers)
    {
        // late name resolution (clips may load after the layer played)
        if (!L.m_current && !L.m_currentName.empty()) L.m_current = find_clip(L.m_currentName);
        if (!L.m_current) continue;

        L.m_time += dt * L.m_speed;
        if (L.m_blendDur > 0.f && L.m_blend < 1.f)
        {
            L.m_blend += dt / L.m_blendDur;
            if (L.m_blend > 1.f) L.m_blend = 1.f;
        }
        if (L.m_blend >= 1.f) L.m_previous = nullptr;

        // one-shot finished: blend back into the return clip
        if (L.finished() && !L.m_returnTo.empty())
        {
            std::string back = L.m_returnTo;
            L.play(back, PlayMode::Loop, 0.2f);
            L.m_current = find_clip(L.m_currentName);
            if (!L.m_current) continue;
        }

        const float tCur = wrapTime(L.m_time, L.m_current->duration(), L.m_mode);
        if (L.m_previous)
        {
            // crossfade: previous at full weight first, current on top by
            // blend factor — net effect lerp(previous, current, blend)
            // the outgoing clip advances under its OWN mode — a finished
            // one-shot must clamp at its last frame, not loop back to the
            // start mid-fade (that read as a bounce)
            L.m_prevTime += dt * L.m_speed;
            const float tPrev = wrapTime(L.m_prevTime, L.m_previous->duration(), L.m_prevMode);
            applyClip(L.m_previous, tPrev, 1.f, L.m_mask, out, m_scratch.data(), n);
            applyClip(L.m_current, tCur, L.m_blend, L.m_mask, out, m_scratch.data(), n);
        }
        else
        {
            applyClip(L.m_current, tCur, 1.f, L.m_mask, out, m_scratch.data(), n);
        }
    }
}
