#include "scene/MorphAnimation.hpp"
#include "scene/Mesh.hpp"
#include <cmath>

const std::string MorphAnimator::kEmpty;

void MorphAnimator::add_clip(const std::string& name, int first, int last, float fps, bool loop)
{
    MorphClip c;
    c.name = name;
    c.first = first;
    c.last = last;
    c.fps = fps;
    c.loop = loop;
    m_clips[name] = c;
}

bool MorphAnimator::play(const std::string& name, float blendTime)
{
    auto it = m_clips.find(name);
    if (it == m_clips.end()) return false;
    // Skip the restart only for a LOOPING clip already playing (repeated
    // play("idle") shouldn't visibly pop/reset a continuous loop). A
    // one-shot clip (loop=false, e.g. "fire"/"reload") must always restart
    // from frame 0 on every play() call, even if it's already the current
    // clip — otherwise, once it reaches its last frame and freezes there
    // (by design, one-shots don't wrap), a second play("reload") was a
    // silent no-op forever: the equality check below matched and returned
    // early without resetting m_current.time, so the clip stayed frozen at
    // its final frame. Only playing a DIFFERENT clip first (make m_current
    // no longer match) could unstick it — exactly the "preciso fazer play
    // fire para reload funcionar" symptom.
    bool sameClip = m_current.clip == &it->second;
    if (sameClip && it->second.loop) return true;

    m_previous = m_current;
    m_current.clip = &it->second;
    m_current.time = 0.f;
    m_blendDuration = blendTime;
    // restarting the SAME one-shot clip (spamming "fire"/"reload") blends
    // it with a slightly-offset copy of itself, and gets worse the faster
    // it's re-triggered — each new click resets m_blendT to 0 before the
    // previous blend settles, so the pose keeps snapping partway back
    // toward frame 0 without ever completing a clean motion ("fica
    // doido"). There's nothing meaningful to crossfade between two points
    // on the same clip, so snap instantly instead; only a genuine change
    // of clip blends smoothly.
    m_blendT = (sameClip || blendTime <= 0.f || !m_previous.clip) ? 1.f : 0.f;
    return true;
}

void MorphAnimator::update(float dt)
{
    if (m_current.clip) m_current.time += dt;
    if (m_previous.clip) m_previous.time += dt;

    if (m_blendT < 1.f)
    {
        m_blendT += (m_blendDuration > 0.f) ? dt / m_blendDuration : 1.f;
        if (m_blendT >= 1.f)
        {
            m_blendT = 1.f;
            m_previous.clip = nullptr; // transition done, drop the old clip
        }
    }
}

void MorphAnimator::frame_pair(const PlayState& s, int& a, int& b, float& t) const
{
    if (!s.clip)
    {
        a = b = 0;
        t = 0.f;
        return;
    }
    const MorphClip& c = *s.clip;
    int span = c.last - c.first; // frames after the first (>=0)
    if (span <= 0)
    {
        a = b = c.first;
        t = 0.f;
        return;
    }
    float framesElapsed = s.time * c.fps;
    if (c.loop)
    {
        float wrapped = fmodf(framesElapsed, (float)(span + 1));
        if (wrapped < 0.f) wrapped += (float)(span + 1);
        int ia = (int)floorf(wrapped);
        t = wrapped - (float)ia;
        a = c.first + ia;
        b = c.first + ((ia + 1) % (span + 1));
    }
    else
    {
        float clamped = framesElapsed;
        if (clamped < 0.f) clamped = 0.f;
        if (clamped > (float)span) clamped = (float)span;
        int ia = (int)floorf(clamped);
        t = clamped - (float)ia;
        a = c.first + ia;
        b = c.first + ((ia < span) ? ia + 1 : ia);
    }
}

void MorphAnimator::write_vertices(const MorphKeyframes& kf, std::vector<MeshVertex>& work) const
{
    const size_t n = kf.vertex_count();
    if (n == 0 || work.size() < n || !m_current.clip) return;

    int a0, b0;
    float t0;
    frame_pair(m_current, a0, b0, t0);
    const std::vector<Vec3>& pa0 = kf.framePos[a0];
    const std::vector<Vec3>& pb0 = kf.framePos[b0];
    const bool hasN = !kf.frameNrm.empty();

    const bool blending = m_blendT < 1.f && m_previous.clip;
    int a1 = 0, b1 = 0;
    float t1 = 0.f;
    if (blending) frame_pair(m_previous, a1, b1, t1);
    const std::vector<Vec3>* pa1 = blending ? &kf.framePos[a1] : nullptr;
    const std::vector<Vec3>* pb1 = blending ? &kf.framePos[b1] : nullptr;

    for (size_t i = 0; i < n; ++i)
    {
        Vec3 posCur = pa0[i].lerp(pb0[i], t0);
        Vec3 nrmCur = hasN ? kf.frameNrm[a0][i].lerp(kf.frameNrm[b0][i], t0) : Vec3(0, 1, 0);

        Vec3 pos = posCur, nrm = nrmCur;
        if (blending)
        {
            Vec3 posPrev = (*pa1)[i].lerp((*pb1)[i], t1);
            pos = posPrev.lerp(posCur, m_blendT);
            if (hasN)
            {
                Vec3 nrmPrev = kf.frameNrm[a1][i].lerp(kf.frameNrm[b1][i], t1);
                nrm = nrmPrev.lerp(nrmCur, m_blendT);
            }
        }
        work[i].position = pos;
        work[i].normal = (nrm.length_squared() > 1e-12f) ? nrm.normalized() : Vec3(0, 1, 0);
    }
}

Mat4 MorphAnimator::tag_transform(const MorphTags& tags, int tagIndex) const
{
    if (tags.empty() || tagIndex < 0 || tagIndex >= (int)tags.names.size())
        return Mat4::Identity();

    // No clip playing (e.g. a static single-frame model that never had
    // add_clip/play called): use the tag's own frame-0 pose instead of
    // identity, so a model doesn't need a fake dummy clip just to expose
    // its tags.
    if (!m_current.clip)
    {
        const MorphTagFrame& f0 = tags.perFrame[0][tagIndex];
        return Mat4::Translate(f0.origin) * Mat4(f0.rotation);
    }

    int a0, b0;
    float t0;
    frame_pair(m_current, a0, b0, t0);
    const MorphTagFrame& fa0 = tags.perFrame[a0][tagIndex];
    const MorphTagFrame& fb0 = tags.perFrame[b0][tagIndex];
    Vec3 pos = fa0.origin.lerp(fb0.origin, t0);
    Quaternion rot = fa0.rotation.nlerp(fb0.rotation, t0);

    const bool blending = m_blendT < 1.f && m_previous.clip;
    if (blending)
    {
        int a1, b1;
        float t1;
        frame_pair(m_previous, a1, b1, t1);
        const MorphTagFrame& fa1 = tags.perFrame[a1][tagIndex];
        const MorphTagFrame& fb1 = tags.perFrame[b1][tagIndex];
        Vec3 posPrev = fa1.origin.lerp(fb1.origin, t1);
        Quaternion rotPrev = fa1.rotation.nlerp(fb1.rotation, t1);
        pos = posPrev.lerp(pos, m_blendT);
        rot = rotPrev.nlerp(rot, m_blendT);
    }

    return Mat4::Translate(pos) * Mat4(rot);
}
