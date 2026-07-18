#pragma once

#include "scene/Math.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneOctree.hpp"
#include "scene/ViewportFit.hpp"
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
class MirrorNode;
class LightNode;
class ParticleSystemNode;
class DecalSystemNode;
class GrassSystemNode;
class TreeSystemNode;
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
    // fill light added on top of the sun term everywhere (forward + terrain
    // shaders), so surfaces facing away from the sun read as dim ambient
    // instead of pure black. Default matches the old hardcoded floor.
    void set_ambient_color(const Vec3& c) { m_ambientColor = c; }

    // ── sky : three kinds, all drawn in every view so water
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
    // tonemap. With ssao on, a hemisphere-kernel ambient occlusion pass
    // darkens contact creases (corners, where objects meet the floor) before
    // godrays — normals are reconstructed from the depth buffer, no G-buffer
    // needed. With bloom on, pixels brighter than the threshold are
    // extracted, blurred (separable: one horizontal pass then one vertical
    // pass, at half resolution — cheap, and a single 9-tap gaussian each way
    // already looks soft since it runs twice) and added back on top, after
    // godrays so the sun's in-scattered light blooms too. Call after init().
    bool enable_post(bool godrays = true, bool ssao = true, bool bloom = true);
    void set_exposure(float e) { m_exposure = e; }
    // debug: toggle SSAO at runtime without reallocating its targets
    void set_ssao_enabled(bool on) { m_ssao_enabled = on; }
    // radius: sample radius in view-space units (bigger = softer/wider AO).
    // strength: pow() exponent on the occlusion factor (bigger = darker).
    void set_ssao_params(float radius, float strength)
    {
        m_ssaoRadius = radius;
        m_ssaoStrength = strength;
    }
    // debug: toggle bloom at runtime without reallocating its targets
    void set_bloom_enabled(bool on) { m_bloom_enabled = on; }
    // threshold: HDR brightness (post-exposure... no, pre-exposure, same
    // units as the scene's own HDR values) above which a pixel starts
    // contributing to the glow. intensity: how much of the blurred result
    // gets added back on top of the sharp image.
    void set_bloom_params(float threshold, float intensity)
    {
        m_bloomThreshold = threshold;
        m_bloomIntensity = intensity;
    }

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
    // debug: wireframe box per SceneOctree node (leaf + internal) that
    // currently holds at least one instance — visualize the culling split
    void set_show_octree_debug(bool on) { m_show_octree_debug = on; }

    // one full frame: extra views (water reflection/refraction), then the
    // main view from the scene's active camera. viewport_w/h set the camera
    // aspect and the GL viewport.
    void render(Scene& scene, int viewport_w, int viewport_h);

    // like render(), but the camera renders at a fixed virtual resolution
    // (virtualW/virtualH — this is what drives the camera aspect, exactly
    // like viewport_w/h above) regardless of the actual window size
    // (windowW/windowH), fit into it per `mode` (see scene/ViewportFit.hpp):
    // Stretch fills the window and distorts, Letterbox fits inside it with
    // black bars, Crop covers it with the overflow clipped. Composition
    // stays identical across window sizes/aspects — only how much of the
    // window shows it (and how much is bars) changes.
    void render_fit(Scene& scene, int windowW, int windowH, int virtualW, int virtualH,
                    ViewportFitMode mode = ViewportFitMode::Letterbox);

    // draws the extra views (water reflection/refraction) as corner overlays
    // — visual proof of what each pass produced
    void set_debug_views(bool on) { m_debug_views = on; }

    int last_item_count() const { return m_last_items; }
    // wall time of the main view's scene.collect() call alone (ms), isolated
    // from GL draw submission — for A/B'ing the spatial index's real CPU cost
    double last_collect_ms() const { return m_lastCollectMs; }

    // Builds (or rebuilds) the object-level spatial index used to accelerate
    // culling in every collect() call this renderer makes (main view + all
    // shadow passes). Static geometry only — call again after adding/moving/
    // removing MeshInstances. Call after the scene is fully populated.
    void build_spatial_index(Scene& scene);
    bool has_spatial_index() const { return m_octree_built; }
    // A/B toggle: use the built index (if any) or fall back to the plain
    // Node-tree walk, without rebuilding — for perf comparisons.
    void set_use_spatial_index(bool on) { m_octree_enabled = on; }
    bool use_spatial_index() const { return m_octree_enabled; }

    // snapshot of the core RenderStats taken at the end of render() — the
    // whole frame's draw calls/tris/binds/state changes, readable by game
    // code without touching gl:: directly
    const gl::RenderStats& frame_stats() const { return m_frameStats; }

    // on-screen stats panel (Batch + embedded font), drawn at the very end
    // of render(). Toggle from the game (F9 in the demos).
    void set_show_stats(bool on) { m_show_stats = on; }
    bool show_stats() const { return m_show_stats; }

    // Immediate-mode wireframe sphere in world space — call after render()
    // each frame you want it visible (nothing persists). Reuses the same
    // lazily-initialized gizmo batch as the light/octree debug draws; no
    // extra setup needed. Meant for live previews (a raycast pick point,
    // the radius a dig/brush would carve, an AI's sense radius, ...) where
    // spinning up a real Mesh/MeshInstance would be overkill.
    void draw_wire_sphere(Camera3D& camera, const Vec3& center, float radius, gl::u8 r = 255, gl::u8 g = 220,
                          gl::u8 b = 60, gl::u8 a = 255);

private:
    void draw_stats(int viewport_w, int viewport_h);
    void draw_light_gizmos(const Mat4& viewProj);
    void draw_octree_debug(const Mat4& viewProj);
    static void collect_octree_bounds(const SceneOctreeNode* node, std::vector<const SceneOctreeNode*>& out);
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
    void draw_mirror_surfaces(const Mat4& view, const Mat4& proj, const Vec3& cameraPos);
    void draw_debug_views(int viewport_w, int viewport_h);
    static void collect_water(Node* node, std::vector<WaterNode*>& out);
    static void collect_mirrors(Node* node, std::vector<MirrorNode*>& out);
    void draw_particles(const Mat4& viewProj);
    static void collect_particles(Node* node, std::vector<ParticleSystemNode*>& out);
    static void collect_decals(Node* node, std::vector<DecalSystemNode*>& out);
    void draw_grass(const Mat4& viewProj);
    static void collect_grass(Node* node, std::vector<GrassSystemNode*>& out);
    void draw_trees(const Mat4& viewProj);
    static void collect_trees(Node* node, std::vector<TreeSystemNode*>& out);
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
    gl::i32 m_locAmbient = -1;
    gl::i32 m_locClipPlane = -1;
    gl::i32 m_locUnlit = -1;
    gl::i32 m_locCameraPos = -1;
    gl::i32 m_locSpecular = -1;
    gl::i32 m_locCascadeMat0 = -1;
    gl::i32 m_locSplits0 = -1;
    gl::i32 m_locCascadeCount = -1;
    gl::i32 m_locShowCascades = -1;
    gl::i32 m_locShadowSize = -1;

    // BSP lightmapped pass (uv2, no tangents, no shadows)
    gl::Shader m_bsp;
    gl::i32 m_locBspModel = -1;
    gl::i32 m_locBspViewProj = -1;
    gl::i32 m_locBspView = -1;
    gl::i32 m_locBspColor = -1;
    gl::i32 m_locBspLightDir = -1;
    gl::i32 m_locBspClipPlane = -1;
    gl::i32 m_locBspCameraPos = -1;

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

    // mirror surface pass (reflection-only, no waves/refraction)
    gl::Shader m_mirror;
    gl::i32 m_locMModel = -1;
    gl::i32 m_locMViewProj = -1;
    gl::i32 m_locMCameraPos = -1;
    gl::i32 m_locMTint = -1;
    gl::i32 m_locMReflectivity = -1;
    gl::i32 m_locMNormal = -1;

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

    // trees: same technique as grass (own shader: atlas-aware, see
    // kTreeVS in SceneShaders.hpp), separate node/system/tuning
    gl::Shader m_tree;
    gl::i32 m_locTreeViewProj = -1;
    std::vector<TreeSystemNode*> m_treeSystems;         // reused across frames
    std::vector<RibbonTrailNode*> m_ribbonTrails;       // reused across frames
    std::vector<TerrainPagingNode*> m_pagedTerrains;    // reused across frames
    std::vector<SkinnedMeshInstance*> m_skinnedMeshes;   // reused across frames

    // skinned forward pass (bone palette in u_bones[])
    gl::Shader m_skinned;
    gl::i32 m_locSkModel = -1, m_locSkViewProj = -1, m_locSkView = -1;
    gl::i32 m_locSkClipPlane = -1, m_locSkBones0 = -1, m_locSkColor = -1;
    gl::i32 m_locSkLightDir = -1, m_locSkCameraPos = -1, m_locSkSpecular = -1;
    gl::i32 m_locSkAmbient = -1;
    gl::i32 m_locSkCascadeMat0 = -1, m_locSkSplits0 = -1, m_locSkCascadeCount = -1;
    gl::Shader m_skinnedDepth; // characters into the CSM cascades
    gl::i32 m_locSkDepthMVP = -1, m_locSkDepthBones0 = -1;

    // BSP lightmapped instances (collected separately, own shader)
    std::vector<class BspInstance*> m_bspInstances;

    // sky passes: procedural gradient, cubemap skybox, equirect skydome
    gl::Shader m_sky;
    gl::Shader m_skyboxShader;
    gl::Shader m_skydomeShader;
    bool m_sky_enabled = false;
    gl::Texture* m_skybox = nullptr;  // not owned
    gl::Texture* m_skydome = nullptr; // not owned

    // post chain: HDR scene target -> (ssao) -> (godrays) -> tonemap -> screen
    bool ensure_post_targets(int w, int h);
    gl::Shader m_tonemap;
    gl::Shader m_godray;
    gl::FrameBuffer m_hdrFbo, m_pingFbo;
    gl::Texture m_hdrColor, m_hdrDepth, m_pingColor;
    // view-space normal G-buffer (COLOR1 of m_hdrFbo), written by the
    // forward mesh/skinned pass alongside m_hdrColor — lets SSAO read real
    // normals instead of reconstructing them from depth derivatives, which
    // goes noisy at distance/grazing angles. RGB16F so a zero vector (the
    // clear value) is an unambiguous "nothing wrote here" sentinel; passes
    // that don't write it (terrain/BSP/water) fall back to the old
    // depth-derivative path for their pixels.
    gl::Texture m_hdrNormal;
    int m_postW = 0, m_postH = 0;

    // set only during render_fit(): where the actual screen framebuffer's
    // viewport is offset to (letterbox/crop) and, since that rect is
    // scaled up/down from the virtual render resolution, how big it
    // actually is on screen — every internal pass that targets an
    // offscreen FBO stays at (0,0,virtualW,virtualH) as usual, only the
    // couple of passes that draw straight to the window (draw_view when
    // v.target is null, and the post chain's final tonemap-to-screen)
    // need the fitted size, otherwise they'd blit the HDR/backbuffer
    // texture into a same-size unscaled viewport instead of stretching it
    // to fill the fitted rect. render() (the plain entry point) leaves
    // m_outputW/H at 0, which the two blit sites treat as "use the
    // passed-in virtual size", so every existing caller is unaffected.
    int m_outputX = 0, m_outputY = 0;
    int m_outputW = 0, m_outputH = 0;
    bool m_post_enabled = false;
    bool m_godrays_enabled = false;
    float m_exposure = 3.4f;

    // bloom: bright-pass extract at half-res, then a small mip chain
    // (each level half the previous) — a single small-radius gaussian blur
    // per level looks like a thin edge glow, not a soft halo, because it
    // can't reach far in pixel terms; downsampling first means the same
    // handful of taps covers a much bigger fraction of the image at each
    // level. Each level is blurred (separable: horizontal then vertical),
    // then the chain is summed back together smallest-to-largest — each
    // upsample is just sampling the smaller texture at the bigger level's
    // UVs, so the hardware's bilinear filter does the upsample for free.
    // The level-0-sized combined result is what gets composited onto the
    // full-res color chain.
    static const int kBloomMips = 4;
    gl::Shader m_bloomExtract, m_bloomDownsample, m_bloomBlur, m_bloomUpsample, m_bloomComposite;
    // m_bloomColor[i]: level i's image — extract/downsample writes it, then
    // the blur pass ends by writing the blurred result back into it too, so
    // after the blur step it always means "level i, blurred". m_bloomBlurFbo/
    // Color[i] is scratch: the blur's horizontal-pass target, then reused as
    // the upsample-add pass's output target on the way back down the chain.
    gl::FrameBuffer m_bloomFbo[kBloomMips], m_bloomBlurFbo[kBloomMips];
    gl::Texture m_bloomColor[kBloomMips], m_bloomBlurColor[kBloomMips];
    int m_bloomMipW[kBloomMips] = {}, m_bloomMipH[kBloomMips] = {};
    bool m_bloom_enabled = false;
    float m_bloomThreshold = 1.0f;
    float m_bloomIntensity = 0.6f;

    // ssao: raw hemisphere-kernel AO into m_ssaoColor, then blurred+composited
    // straight into the ping-pong color chain (see render())
    gl::Shader m_ssao, m_ssaoBlur;
    gl::FrameBuffer m_ssaoFbo;
    gl::Texture m_ssaoColor, m_ssaoNoise;
    bool m_ssao_enabled = false;
    float m_ssaoRadius = 0.5f;
    float m_ssaoStrength = 0.8f; // softer default — less visible if any tiling residue remains

    // ocean surface pass (Gerstner shader );
    // uniforms are set by name — the pass runs once per ocean per frame
    gl::Shader m_oceanShader;
    bool m_ocean_ready = false;

    gl::Texture m_white;    // 1x1 fallback so u_diffuse always samples something
    gl::Texture m_gray;     // 1x1 neutral detail map
    std::vector<class LensFlareNode*> m_lensflares; // reused across frames

    // terrain shader (texture splatting)
    gl::Shader m_terrainShader;
    gl::i32 m_locTModel = -1;
    gl::i32 m_locTViewProj = -1;
    gl::i32 m_locTView = -1;
    gl::i32 m_locTLightDir = -1;
    gl::i32 m_locTAmbient = -1;
    gl::i32 m_locTClipPlane = -1;

    Vec3 m_clearColor = Vec3(0.5f, 0.65f, 0.8f);
    Vec3 m_lightDir = Vec3(0.5f, -1.0f, 0.3f);
    Vec3 m_ambientColor = Vec3(0.30f, 0.30f, 0.32f);
    std::vector<RenderItem> m_items;  // reused across views
    std::vector<WaterNode*> m_waters;   // reused across frames
    std::vector<MirrorNode*> m_mirrors; // reused across frames
    int m_last_items = 0;
    double m_lastCollectMs = 0.0;
    gl::RenderStats m_frameStats;
    gl::Batch m_statsBatch; // lazy-init 2D batch for the stats panel
    bool m_statsBatchReady = false;
    gl::Batch m_gizmoBatch; // lazy-init 3D batch for light gizmos
    bool m_gizmoBatchReady = false;
    bool m_show_light_gizmos = false;
    bool m_show_octree_debug = false;
    bool m_show_stats = false;
    gl::u64 m_lastFrameNs = 0; // render()-to-render() clock for the fps line
    float m_smoothMs = 0.f;    // exponentially smoothed frame time
    bool m_ready = false;
    SceneOctree m_octree;
    bool m_octree_built = false;
    bool m_octree_enabled = true;
    bool m_debug_views = false;
};
