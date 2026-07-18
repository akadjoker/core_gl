#pragma once

// A single manually-placed billboard quad — for one-off effects (a flame,
// a marker, a static poster) that don't fit ParticleSystemNode's
// emission/lifetime model (spawn, live, die) and don't need
// GrassSystemNode/TreeSystemNode's mass-scattering. Reuses the same
// world-space-quad-with-color-in-tangent mesh layout and the particle
// shader/blend state in SceneRenderer, so no new shader exists just for
// this — only the per-instance orientation math differs.
//
// Three view modes (same techniques Blitz3D's Sprite entity offers):
//  - Free:    fully camera-facing, like a particle — rotates on every axis
//             to stay flat toward the viewer.
//  - Upright: faces the camera's yaw only, world "up" stays fixed — the
//             standard tree/grass billboard, doesn't tilt when the camera
//             looks up/down.
//  - Fixed:   no auto-facing at all; renders flat in the node's own world
//             orientation (set_rotation()), like a regular oriented quad.
//
// Atlas: set_uv_rect() (or set_atlas_grid()+pick a cell) samples a
// sub-rect of the texture instead of the whole thing — one sprite sheet
// can serve many different BillboardNodes (or the same one switching
// frames over time), same convention ParticleSystemNode's per-particle
// texRect already uses.

#include "scene/AtlasAnimator.hpp"
#include "scene/Mesh.hpp"
#include "scene/Node3D.hpp"
#include "scene/ParticleSystemNode.hpp" // ParticleBlendMode

namespace gl
{
class Texture;
}

enum class BillboardViewMode
{
    Free,
    Upright,
    Fixed
};

class BillboardNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_BILLBOARD;

    explicit BillboardNode(const std::string& name = "billboard");

    bool is_a(NodeType t) const override { return t == NT_BILLBOARD || Node3D::is_a(t); }

    void set_size(float w, float h) { m_size = Vec2(w, h); }
    void set_size(const Vec2& s) { m_size = s; }
    void set_color(const Vec4& c) { m_color = c; }
    void set_view_mode(BillboardViewMode m) { m_viewMode = m; }
    BillboardViewMode view_mode() const { return m_viewMode; }

    // atlas rect in normalized [0,1] UV space: (u0, v0, width, height).
    // Default (0,0,1,1) samples the whole texture. Switches off animation
    // (see set_animated_grid()) if it was on.
    void set_uv_rect(float u0, float v0, float w, float h)
    {
        m_uvRect = Vec4(u0, v0, w, h);
        m_animated = false;
    }
    // convenience: an NxM grid, pick a fixed cell (col,row) — 0-based,
    // clamped. Switches off animation if it was on.
    void set_atlas_grid(int cols, int rows, int col, int row)
    {
        cols = cols > 0 ? cols : 1;
        rows = rows > 0 ? rows : 1;
        col = col < 0 ? 0 : (col >= cols ? cols - 1 : col);
        row = row < 0 ? 0 : (row >= rows ? rows - 1 : row);
        float w = 1.f / (float)cols, h = 1.f / (float)rows;
        m_uvRect = Vec4((float)col * w, (float)row * h, w, h);
        m_animated = false;
    }
    // flip-book animation: cycles every cell of an NxM atlas at `fps`
    // frames/second, looping forever (e.g. a fire/explosion sprite sheet).
    // Overrides set_uv_rect()/set_atlas_grid() until one of those is
    // called again.
    void set_animated_grid(int cols, int rows, float fps)
    {
        m_animator.set_grid(cols, rows);
        m_animator.fps = fps;
        m_animated = true;
    }

    gl::Texture* texture = nullptr; // non-owning (AssetManager)
    ParticleBlendMode blend = ParticleBlendMode::Additive;
    // depth test off suits additive fx (fire, glow); on suits an opaque-ish
    // cutout poster/marker that should hide behind real geometry
    bool depthTest = true;

    // ── renderer-side (game code never calls these) ──
    bool ensure_gpu();
    // camRight/camUp: active camera's world-space right/up axes (Free
    // mode uses both directly); camForward drives Upright's yaw-only
    // facing. Fixed ignores all three.
    void rebuild(const Vec3& camRight, const Vec3& camUp, const Vec3& camForward);
    Mesh& quad_mesh() { return m_mesh; }

protected:
    void _update(float dt) override
    {
        if (m_animated) m_animator.update(dt);
    }
    void _release_gpu() override { m_mesh.release_gpu(); }

private:
    Vec2 m_size{1.f, 1.f};
    Vec4 m_color{1.f, 1.f, 1.f, 1.f};
    Vec4 m_uvRect{0.f, 0.f, 1.f, 1.f};
    BillboardViewMode m_viewMode = BillboardViewMode::Free;
    AtlasAnimator m_animator;
    bool m_animated = false;
    Mesh m_mesh;
    bool m_gpu_ready = false;
};
