#pragma once

#include "scene/Math.hpp"
#include "scene/Scene.hpp"
#include <coregl/gl_shader.hpp>
#include <coregl/gl_texture.hpp>
#include <vector>

namespace gl
{
class FrameBuffer;
}
class Camera3D;
class WaterNode;

// Draws a Scene. This is the engine-side boundary: game code builds the node
// tree and calls render() — it never touches coregl directly.
//
// The pipeline is fixed; the number of VIEWS is not. A RenderView is one
// rendering of the scene from one point of view into one target. The main
// view (active camera → screen) always runs; special nodes add extra views
// before it: each WaterNode adds a mirrored reflection view and a clipped
// refraction view into its own textures, which the water surface then
// samples in the main pass. Shadow cascades and mirrors join the same loop
// later — all invisible to the caller.
class SceneRenderer
{
public:
    SceneRenderer() = default;
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    bool init();    // compiles the built-in shaders; needs a live GL context
    void release(); // frees GL objects (call before the context dies)

    void set_clear_color(float r, float g, float b);
    void set_light_dir(const Vec3& dir); // direction the light travels

    // one full frame: extra views (water reflection/refraction), then the
    // main view from the scene's active camera. viewport_w/h set the camera
    // aspect and the GL viewport.
    void render(Scene& scene, int viewport_w, int viewport_h);

    // draws the extra views (water reflection/refraction) as corner overlays
    // — visual proof of what each pass produced
    void set_debug_views(bool on) { m_debug_views = on; }

    int last_item_count() const { return m_last_items; }

private:
    // one rendering of the scene into one target
    struct RenderView
    {
        Mat4 view;
        Mat4 proj;
        gl::FrameBuffer* target = nullptr; // null = screen
        int w = 0, h = 0;
        // world plane; fragments with dot(p,plane) < 0 are clipped. The
        // default (0,0,0,1) means "keep everything" — required by the ES
        // in-shader clip path, which has no separate enable switch.
        Vec4 clip_plane = Vec4(0.f, 0.f, 0.f, 1.f);
        bool use_clip = false;
        bool mirrored = false; // planar reflection: flips front-face winding
    };

    void draw_view(Scene& scene, const RenderView& v);
    void draw_water_surfaces(const Mat4& viewProj, const Vec3& cameraPos, float camNear,
                             float camFar);
    void draw_debug_views(int viewport_w, int viewport_h);
    static void collect_water(Node* node, std::vector<WaterNode*>& out);

    // forward pass
    gl::Shader m_forward;
    gl::i32 m_locModel = -1;
    gl::i32 m_locViewProj = -1;
    gl::i32 m_locColor = -1;
    gl::i32 m_locLightDir = -1;
    gl::i32 m_locClipPlane = -1;
    gl::i32 m_locUnlit = -1;

    // water surface pass
    gl::Shader m_water;
    gl::i32 m_locWModel = -1;
    gl::i32 m_locWViewProj = -1;
    gl::i32 m_locWCameraPos = -1;
    gl::i32 m_locWLightDir = -1;
    gl::i32 m_locWTime = -1;
    gl::i32 m_locWColor = -1;
    gl::i32 m_locWDistortion = -1;
    gl::i32 m_locWTiling = -1;
    gl::i32 m_locWColorMix = -1;
    gl::i32 m_locWCamPlanes = -1;

    // debug overlay (positioned quads showing the extra views' textures)
    gl::Shader m_debug;
    gl::i32 m_locDRect = -1;
    gl::i32 m_locDTargetSize = -1;

    gl::Texture m_white; // 1x1 fallback so u_diffuse always samples something

    Vec3 m_clearColor = Vec3(0.5f, 0.65f, 0.8f);
    Vec3 m_lightDir = Vec3(0.5f, -1.0f, 0.3f);
    std::vector<RenderItem> m_items;  // reused across views
    std::vector<WaterNode*> m_waters; // reused across frames
    int m_last_items = 0;
    bool m_ready = false;
    bool m_debug_views = false;
};
