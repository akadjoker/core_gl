#pragma once

#include "scene/Math.hpp"
#include "scene/Scene.hpp"
#include <coregl/gl_batch.hpp>
#include <coregl/gl_framebuffer.hpp>
#include <coregl/gl_shader.hpp>
#include <coregl/gl_texture.hpp>
#include <vector>

namespace gl
{
class FrameBuffer;
}
class Camera3D;
class WaterNode;
class LightNode;
class ParticleSystemNode;
class DecalSystemNode;
class GrassSystemNode;
class RibbonTrailNode;
class TerrainPagingNode;
class SkinnedMeshInstance;

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

    // ── sky (Ogre-style: three kinds, all drawn in every view so water
    // reflections show them too). Lighting is independent: set_light_dir
    // keeps driving sun light/shadows/fog whatever the background is.
    // procedural: gradient + sun disc derived from the light direction —
    // animate set_light_dir and dawn/day/dusk/night follow
    void set_sky_enabled(bool on)
    {
        m_sky_enabled = on;
        if (on) m_skybox = nullptr, m_skydome = nullptr;
    }
    // skybox: 6-face cubemap sampled by view direction (null disables)
    void set_skybox(gl::Texture* cubemap)
    {
        m_skybox = cubemap;
        if (cubemap) m_sky_enabled = false, m_skydome = nullptr;
    }
    // skydome: equirectangular panorama texture (null disables)
    void set_skydome(gl::Texture* panorama)
    {
        m_skydome = panorama;
        if (panorama) m_sky_enabled = false, m_skybox = nullptr;
    }

    // ── post-processing ──
    // The main view renders into an HDR target; a filmic tonemap brings it
    // to the screen (strong lights roll off instead of clipping). With
    // godrays on (needs enable_shadows), a volumetric pass marches the last
    // shadow cascade and adds the sun's in-scattered light before the
    // tonemap. Call after init().
    bool enable_post(bool godrays = true);
    void set_exposure(float e) { m_exposure = e; }

    // ── directional-light shadows (CSM) ──
    // Call once after init(). Splits the camera frustum into `cascades`
    // slices, each with its own depth map layer fitted tightly around it
    // (texel-snapped). `distance` caps how far shadows reach — smaller
    // means sharper shadows for the same resolution.
    bool enable_shadows(int cascades = 4, int resolution = 2048, float distance = 200.f);
    void set_show_cascades(bool on) { m_show_cascades = on; } // debug tint
    // debug: fully disable/re-enable shadow sampling in the forward shader
    // without tearing down the shadow map (toggle at runtime, no realloc)
    void set_shadows_active(bool on) { m_shadows_active = on; }
    // debug: wireframe sphere at every LightNode (radius = range for point/
    // spot; a fixed small radius for directional-only scenes with none) so
    // you can see where a light actually sits instead of guessing from the
    // lit result. Drawn last, on top, in every view.
    void set_show_light_gizmos(bool on) { m_show_light_gizmos = on; }

    // one full frame: extra views (water reflection/refraction), then the
    // main view from the scene's active camera. viewport_w/h set the camera
    // aspect and the GL viewport.
    void render(Scene& scene, int viewport_w, int viewport_h);

    // draws the extra views (water reflection/refraction) as corner overlays
    // — visual proof of what each pass produced
    void set_debug_views(bool on) { m_debug_views = on; }

    int last_item_count() const { return m_last_items; }

    // snapshot of the core RenderStats taken at the end of render() — the
    // whole frame's draw calls/tris/binds/state changes, readable by game
    // code without touching gl:: directly
    const gl::RenderStats& frame_stats() const { return m_frameStats; }

    // on-screen stats panel (Batch + embedded font), drawn at the very end
    // of render(). Toggle from the game (F9 in the demos).
    void set_show_stats(bool on) { m_show_stats = on; }
    bool show_stats() const { return m_show_stats; }

private:
    void draw_stats(int viewport_w, int viewport_h);
    void draw_light_gizmos(const Mat4& viewProj);
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
        Vec3 cam_pos;          // eye position for this view (specular)
    };

    void draw_view(Scene& scene, const RenderView& v);
    void draw_ocean_surface(class OceanNode* ocean, const Mat4& view, const Mat4& proj,
                            const Vec3& cameraPos);
    void draw_shadow_views(Scene& scene, Camera3D* camera);
    void draw_light_shadows(Scene& scene); // point cubemaps + spot maps
    void set_light_uniforms();
    static void collect_lights(Node* node, std::vector<LightNode*>& out);
    void draw_water_surfaces(const Mat4& view, const Mat4& proj, const Vec3& cameraPos,
                             float camNear, float camFar);
    void draw_debug_views(int viewport_w, int viewport_h);
    static void collect_water(Node* node, std::vector<WaterNode*>& out);
    void draw_particles(const Mat4& viewProj);
    static void collect_particles(Node* node, std::vector<ParticleSystemNode*>& out);
    static void collect_decals(Node* node, std::vector<DecalSystemNode*>& out);
    void draw_grass(const Mat4& viewProj);
    static void collect_grass(Node* node, std::vector<GrassSystemNode*>& out);
    void draw_ribbontrails(const Mat4& viewProj);
    static void collect_ribbontrails(Node* node, std::vector<RibbonTrailNode*>& out);
    // splat-mode paged terrain, drawn inside every view (reflections too)
    void draw_paged_terrain(const RenderView& v, const Frustum& frustum);
    // GPU-skinned characters (kSkinnedVS + the forward FS)
    void draw_skinned(const RenderView& v, const Frustum& frustum);
    static void collect_skinned(Node* node, std::vector<SkinnedMeshInstance*>& out);
    static void collect_paged_terrain(Node* node, std::vector<TerrainPagingNode*>& out);
    void draw_lensflares(Camera3D& cam);
    static void collect_lensflares(Node* node, std::vector<class LensFlareNode*>& out);

    // forward pass
    gl::Shader m_forward;
    gl::i32 m_locModel = -1;
    gl::i32 m_locViewProj = -1;
    gl::i32 m_locView = -1;
    gl::i32 m_locColor = -1;
    gl::i32 m_locLightDir = -1;
    gl::i32 m_locClipPlane = -1;
    gl::i32 m_locUnlit = -1;
    gl::i32 m_locCameraPos = -1;
    gl::i32 m_locSpecular = -1;
    gl::i32 m_locCascadeMat0 = -1;
    gl::i32 m_locSplits0 = -1;
    gl::i32 m_locCascadeCount = -1;
    gl::i32 m_locShowCascades = -1;
    gl::i32 m_locShadowSize = -1;

    // shadow pass (CSM): depth-only views into a depth array, one layer per
    // cascade, consumed by the forward shader
    gl::Shader m_depth;
    gl::i32 m_locDepthMVP = -1;
    gl::Texture m_shadowTex;
    gl::FrameBuffer m_shadowFbo;
    int m_cascades = 0; // 0 = shadows disabled
    int m_shadowSize = 0;
    float m_shadow_distance = 200.f;
    float m_splits[5] = {};
    Mat4 m_cascadeMat[4];
    bool m_show_cascades = false;
    bool m_shadows_active = true;
    std::vector<RenderItem> m_shadow_items; // reused across frames

    // local lights (point/spot): depth pass writing linear distance
    gl::Shader m_pointDepth;
    gl::i32 m_locPDModel = -1;
    gl::i32 m_locPDLightVP = -1;
    gl::i32 m_locPDLightPos = -1;
    gl::i32 m_locPDRange = -1;
    gl::i32 m_locPointCount = -1;
    gl::i32 m_locPointPosRange0 = -1;
    gl::i32 m_locPointColor0 = -1;
    gl::i32 m_locSpotCount = -1;
    gl::i32 m_locSpotPosRange0 = -1;
    gl::i32 m_locSpotDirInner0 = -1;
    gl::i32 m_locSpotColorOuter0 = -1;
    gl::i32 m_locSpotShadowSlot0 = -1;
    gl::i32 m_locSpotShadowMat0 = -1;
    std::vector<LightNode*> m_lights; // reused across frames
    Mat4 m_spotShadowMat[2];

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

    // particle billboards: vertices already baked in world space by each
    // ParticleSystemNode, so the shader only applies viewProj
    gl::Shader m_particle;
    gl::i32 m_locPViewProj = -1;
    std::vector<ParticleSystemNode*> m_particleSystems; // reused across frames
    std::vector<DecalSystemNode*> m_decalSystems;       // reused across frames

    // grass: static mesh per node, wind-sway + alpha-cutout shader
    gl::Shader m_grass;
    gl::i32 m_locGViewProj = -1;
    std::vector<GrassSystemNode*> m_grassSystems;       // reused across frames
    std::vector<RibbonTrailNode*> m_ribbonTrails;       // reused across frames
    std::vector<TerrainPagingNode*> m_pagedTerrains;    // reused across frames
    std::vector<SkinnedMeshInstance*> m_skinnedMeshes;   // reused across frames

    // skinned forward pass (bone palette in u_bones[])
    gl::Shader m_skinned;
    gl::i32 m_locSkModel = -1, m_locSkViewProj = -1, m_locSkView = -1;
    gl::i32 m_locSkClipPlane = -1, m_locSkBones0 = -1, m_locSkColor = -1;
    gl::i32 m_locSkLightDir = -1, m_locSkCameraPos = -1, m_locSkSpecular = -1;
    gl::i32 m_locSkCascadeMat0 = -1, m_locSkSplits0 = -1, m_locSkCascadeCount = -1;
    gl::Shader m_skinnedDepth; // characters into the CSM cascades
    gl::i32 m_locSkDepthMVP = -1, m_locSkDepthBones0 = -1;

    // sky passes: procedural gradient, cubemap skybox, equirect skydome
    gl::Shader m_sky;
    gl::Shader m_skyboxShader;
    gl::Shader m_skydomeShader;
    bool m_sky_enabled = false;
    gl::Texture* m_skybox = nullptr;  // not owned
    gl::Texture* m_skydome = nullptr; // not owned

    // post chain: HDR scene target -> (godrays) -> tonemap -> screen
    bool ensure_post_targets(int w, int h);
    gl::Shader m_tonemap;
    gl::Shader m_godray;
    gl::FrameBuffer m_hdrFbo, m_pingFbo;
    gl::Texture m_hdrColor, m_hdrDepth, m_pingColor;
    int m_postW = 0, m_postH = 0;
    bool m_post_enabled = false;
    bool m_godrays_enabled = false;
    float m_exposure = 3.4f;

    // ocean surface pass (Gerstner shader from the reference assets);
    // uniforms are set by name — the pass runs once per ocean per frame
    gl::Shader m_oceanShader;
    bool m_ocean_ready = false;

    gl::Texture m_white;    // 1x1 fallback so u_diffuse always samples something
    gl::Texture m_gray;     // 1x1 neutral detail map
    std::vector<class LensFlareNode*> m_lensflares; // reused across frames

    // terrain shader (texture splatting, Ogre-style)
    gl::Shader m_terrainShader;
    gl::i32 m_locTModel = -1;
    gl::i32 m_locTViewProj = -1;
    gl::i32 m_locTView = -1;
    gl::i32 m_locTLightDir = -1;
    gl::i32 m_locTAmbient = -1;
    gl::i32 m_locTClipPlane = -1;

    Vec3 m_clearColor = Vec3(0.5f, 0.65f, 0.8f);
    Vec3 m_lightDir = Vec3(0.5f, -1.0f, 0.3f);
    std::vector<RenderItem> m_items;  // reused across views
    std::vector<WaterNode*> m_waters; // reused across frames
    int m_last_items = 0;
    gl::RenderStats m_frameStats;
    gl::Batch m_statsBatch; // lazy-init 2D batch for the stats panel
    bool m_statsBatchReady = false;
    gl::Batch m_gizmoBatch; // lazy-init 3D batch for light gizmos
    bool m_gizmoBatchReady = false;
    bool m_show_light_gizmos = false;
    bool m_show_stats = false;
    gl::u64 m_lastFrameNs = 0; // render()-to-render() clock for the fps line
    float m_smoothMs = 0.f;    // exponentially smoothed frame time
    bool m_ready = false;
    bool m_debug_views = false;
};
