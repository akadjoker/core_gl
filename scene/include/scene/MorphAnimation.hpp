#pragma once

#include "scene/Math.hpp"
#include <string>
#include <unordered_map>
#include <vector>

struct MeshVertex;

// Per-vertex keyframes for MD2/MD3-style vertex (morph) animation: one
// position+normal per vertex per frame, same vertex order/count as the
// owning Mesh's vertex buffer. No skeleton — the "bone" here is just a
// frame index.
struct MorphKeyframes
{
    std::vector<std::vector<Vec3>> framePos;
    std::vector<std::vector<Vec3>> frameNrm;

    int frame_count() const { return (int)framePos.size(); }
    size_t vertex_count() const { return framePos.empty() ? 0 : framePos[0].size(); }
};

// MD3 tags: named per-frame attachment points (origin + rotation), used to
// glue separate model parts together (lower->tag_torso->upper->tag_head->
// head, tag_weapon->gun, ...). Empty/unused for MD2.
struct MorphTagFrame
{
    Vec3 origin;
    Quaternion rotation;
};

struct MorphTags
{
    std::vector<std::string> names;
    std::vector<std::vector<MorphTagFrame>> perFrame; // [frame][tagIndex]

    bool empty() const { return names.empty(); }
    int find(const std::string& name) const
    {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == name) return (int)i;
        return -1;
    }
};

// A named frame range, e.g. "run" = frames 40..47 at 10fps, looping —
// exactly the Quake animation.cfg model (start/count/fps/loop), just
// specified directly instead of parsed from the cfg file.
struct MorphClip
{
    std::string name;
    int first = 0;
    int last = 0; // inclusive
    float fps = 10.f;
    bool loop = true;
};

// Drives a MorphKeyframes/MorphTags pair over time: advances a current clip
// (looping or one-shot), computes the frame-pair + blend factor, and can
// crossfade smoothly from whatever was playing before into a newly
// requested clip (two clips blended together, each itself interpolating
// its own two keyframes — the one real extension beyond simple frame
// interpolation). Pure CPU math, no GPU/mesh coupling: the caller
// (MorphMeshInstance) owns the Mesh and pushes the result via
// Mesh::update_vertices().
class MorphAnimator
{
public:
    void add_clip(const std::string& name, int first, int last, float fps, bool loop = true);
    bool has_clip(const std::string& name) const { return m_clips.find(name) != m_clips.end(); }

    // crossfades from the current clip (if any) into `name` over blendTime
    // seconds. No-op if `name` is already the current clip.
    bool play(const std::string& name, float blendTime = 0.15f);
    const std::string& current_clip() const { return m_current.clip ? m_current.clip->name : kEmpty; }

    void update(float dt);

    // blends framePos/frameNrm into `work` (position+normal only — uv/
    // tangent are left untouched, the caller pre-fills those once from the
    // base mesh). `work` must already be sized to kf.vertex_count().
    void write_vertices(const MorphKeyframes& kf, std::vector<MeshVertex>& work) const;

    // local transform (translation * rotation) of tag `tagIndex` at the
    // current blended pose. Identity if `tags` has no frames.
    Mat4 tag_transform(const MorphTags& tags, int tagIndex) const;

private:
    struct PlayState
    {
        const MorphClip* clip = nullptr;
        float time = 0.f; // seconds into the clip
    };

    // resolves a PlayState to a (frame a, frame b, blend t) pair, handling
    // loop wraparound / once clamping.
    void frame_pair(const PlayState& s, int& a, int& b, float& t) const;

    std::unordered_map<std::string, MorphClip> m_clips;
    PlayState m_current, m_previous;
    float m_blendT = 1.f;        // 0 = fully previous, 1 = fully current
    float m_blendDuration = 0.f;

    static const std::string kEmpty;
};
