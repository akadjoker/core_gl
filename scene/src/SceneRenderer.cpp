#include "scene/SceneRenderer.hpp"
#include "scene/Camera3D.hpp"
#include "scene/Material.hpp"
#include "scene/WaterNode.hpp"
#include <coregl/gl_framebuffer.hpp>
#include <coregl/gl_renderer.hpp>
#include <coregl/gl_vertex_array.hpp>

// ── built-in forward shader (lambert + ambient, diffuse map, clip plane) ──
// All shader bodies are version-less: Renderer::ShaderHeader() is prepended
// at init, providing the right GLSL version, precision and clip macros for
// the platform (desktop GL / ES / WebGL2).
static const char* kFwdVS = R"(
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform vec4 u_clipPlane;
out vec3 v_normal;
out vec2 v_uv;
CLIP_VARYING
void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_normal = normalize(mat3(u_model) * a_normal);
    v_uv = a_uv;
    CLIP_SETUP(dot(worldPos, u_clipPlane));
    gl_Position = u_viewProj * worldPos;
}
)";

static const char* kFwdFS = R"(
in vec3 v_normal;
in vec2 v_uv;
out vec4 OutColor;
uniform vec3 u_baseColor;
uniform vec3 u_lightDir;
uniform float u_unlit;
uniform sampler2D u_diffuse;
CLIP_VARYING
void main()
{
    CLIP_APPLY;
    vec3 albedo = texture(u_diffuse, v_uv).rgb * u_baseColor;
    float diffuse = max(dot(normalize(v_normal), -u_lightDir), 0.0);
    float light = mix(0.30 + 0.70 * diffuse, 1.0, u_unlit);
    OutColor = vec4(albedo * light, 1.0);
}
)";

// ── water surface shader: projective reflection/refraction + fresnel ──
static const char* kWaterVS = R"(
layout(location = 0) in vec3 a_position;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform float u_tiling;
out vec4 v_clip;
out vec2 v_uv;
out vec3 v_world;
void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_world = worldPos.xyz;
    v_uv = a_uv * u_tiling;
    v_clip = u_viewProj * worldPos;
    gl_Position = v_clip;
}
)";

static const char* kWaterFS = R"(
in vec4 v_clip;
in vec2 v_uv;
in vec3 v_world;
out vec4 OutColor;
uniform sampler2D u_reflection;
uniform sampler2D u_refraction;
uniform sampler2D u_refractionDepth;
uniform vec3 u_cameraPos;
uniform vec3 u_lightDir;
uniform vec3 u_waterColor;
uniform float u_time;
uniform float u_distortion;
uniform float u_colorMix;
uniform vec2 u_camPlanes; // near, far — to linearize depth

float linearDepth(float d)
{
    float z = d * 2.0 - 1.0;
    return 2.0 * u_camPlanes.x * u_camPlanes.y /
           (u_camPlanes.y + u_camPlanes.x - z * (u_camPlanes.y - u_camPlanes.x));
}

void main()
{
    vec2 ndc = (v_clip.xy / v_clip.w) * 0.5 + 0.5;

    // per-pixel water depth: distance from the surface down to whatever the
    // refraction view saw at this pixel
    float floorDist = linearDepth(texture(u_refractionDepth, ndc).r);
    float waterDist = linearDepth(gl_FragCoord.z);
    float waterDepth = floorDist - waterDist;
    // shoreline factor: 0 right at the shore, 1 in open water
    float soft = clamp(waterDepth / 1.5, 0.0, 1.0);

    // procedural dudv: two scrolling wave fields, calmed near the shore
    float t = u_time;
    vec2 d1 = vec2(sin(v_uv.y * 12.3 + t * 4.0), cos(v_uv.x * 11.7 + t * 3.4));
    vec2 d2 = vec2(sin((v_uv.x + v_uv.y) * 9.1 - t * 2.7),
                   cos((v_uv.y - v_uv.x) * 10.3 + t * 3.9));
    vec2 distortion = (d1 + d2) * 0.25 * u_distortion * soft;

    vec2 reflUV = clamp(vec2(ndc.x, 1.0 - ndc.y) + distortion, 0.001, 0.999);
    vec2 refrUV = clamp(ndc + distortion, 0.001, 0.999);
    vec3 refl = texture(u_reflection, reflUV).rgb;
    vec3 refr = texture(u_refraction, refrUV).rgb;

    // fresnel: looking straight down favors refraction, grazing favors reflection
    vec3 viewDir = normalize(u_cameraPos - v_world);
    float fresnel = pow(clamp(dot(viewDir, vec3(0.0, 1.0, 0.0)), 0.0, 1.0), 0.7);

    vec3 color = mix(refl, refr, fresnel);
    // tint grows with depth: shallow water stays clear, deep water murks up
    float murk = clamp(u_colorMix + waterDepth * 0.02, 0.0, 0.75);
    color = mix(color, u_waterColor, murk);

    // sun glint from the distorted surface normal, faded out at the shore
    vec3 n = normalize(vec3(distortion.x * 8.0, 1.0, distortion.y * 8.0));
    vec3 h = normalize(viewDir - u_lightDir);
    color += vec3(0.5 * pow(max(dot(n, h), 0.0), 120.0) * soft);

    // soft edge: the surface fades to nothing where it meets the terrain
    OutColor = vec4(color, clamp(waterDepth / 0.9, 0.0, 1.0));
}
)";

// debug overlay: shows an extra view's texture in a corner rect
static const char* kDebugFS = R"(
in vec2 v_uv;
out vec4 OutColor;
uniform sampler2D u_tex;
void main()
{
    // shown raw (no vertical flip): a reflection view reads upside down,
    // which makes it obvious the overlay is the extra view, not the scene
    OutColor = vec4(texture(u_tex, v_uv).rgb, 1.0);
}
)";

// prepends the platform shader header to a version-less body
static bool loadStage(gl::Shader& shader, gl::PipelineStage stage, const char* body)
{
    std::string src = std::string(gl::Renderer::ShaderHeader(stage)) + body;
    return shader.LoadFromString(stage, src.c_str());
}

bool SceneRenderer::init()
{
    if (!loadStage(m_forward, gl::PipelineStage::VERTEX, kFwdVS) ||
        !loadStage(m_forward, gl::PipelineStage::FRAGMENT, kFwdFS) || !m_forward.Link())
        return false;
    if (!loadStage(m_water, gl::PipelineStage::VERTEX, kWaterVS) ||
        !loadStage(m_water, gl::PipelineStage::FRAGMENT, kWaterFS) || !m_water.Link())
        return false;

    m_locModel = m_forward.GetLocation("u_model");
    m_locViewProj = m_forward.GetLocation("u_viewProj");
    m_locColor = m_forward.GetLocation("u_baseColor");
    m_locLightDir = m_forward.GetLocation("u_lightDir");
    m_locClipPlane = m_forward.GetLocation("u_clipPlane");
    m_locUnlit = m_forward.GetLocation("u_unlit");
    m_forward.Bind();
    m_forward.SetInt("u_diffuse", 0);

    m_locWModel = m_water.GetLocation("u_model");
    m_locWViewProj = m_water.GetLocation("u_viewProj");
    m_locWCameraPos = m_water.GetLocation("u_cameraPos");
    m_locWLightDir = m_water.GetLocation("u_lightDir");
    m_locWTime = m_water.GetLocation("u_time");
    m_locWColor = m_water.GetLocation("u_waterColor");
    m_locWDistortion = m_water.GetLocation("u_distortion");
    m_locWTiling = m_water.GetLocation("u_tiling");
    m_locWColorMix = m_water.GetLocation("u_colorMix");
    m_locWCamPlanes = m_water.GetLocation("u_camPlanes");
    m_water.Bind();
    m_water.SetInt("u_reflection", 0);
    m_water.SetInt("u_refraction", 1);
    m_water.SetInt("u_refractionDepth", 2);

    if (!m_debug.LoadFromString(gl::PipelineStage::VERTEX, gl::Renderer::QuadShaderSource()) ||
        !loadStage(m_debug, gl::PipelineStage::FRAGMENT, kDebugFS) || !m_debug.Link())
        return false;
    m_locDRect = m_debug.GetLocation("u_rect");
    m_locDTargetSize = m_debug.GetLocation("u_targetSize");
    m_debug.Bind();
    m_debug.SetInt("u_tex", 0);

    const gl::u8 white[4] = {255, 255, 255, 255};
    m_white.Load2D(white, 1, 1, gl::TextureFormat::RGBA8);

    m_items.reserve(256);
    m_ready = true;
    return true;
}

void SceneRenderer::release()
{
    m_forward.Release();
    m_water.Release();
    m_debug.Release();
    m_white.Release();
    m_ready = false;
}

void SceneRenderer::set_clear_color(float r, float g, float b)
{
    m_clearColor = Vec3(r, g, b);
}

void SceneRenderer::set_light_dir(const Vec3& dir)
{
    m_lightDir = dir.normalized();
}

void SceneRenderer::collect_water(Node* node, std::vector<WaterNode*>& out)
{
    WaterNode* w = node->as<WaterNode>();
    if (w) out.push_back(w);
    for (Node* child : node->get_children())
        collect_water(child, out);
}

void SceneRenderer::draw_view(Scene& scene, const RenderView& v)
{
    // cull against this view's own frustum
    Frustum frustum;
    frustum.build(v.view, v.proj);
    m_items.clear();
    scene.collect(m_items, &frustum);
    m_last_items = (int)m_items.size();

    if (v.target)
        v.target->Bind();
    else
        gl::Renderer::BindScreen();
    gl::Renderer::Viewport(0, 0, v.w, v.h);
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetCull(gl::CullMode::BACK);
    // a mirrored view reverses triangle winding: flip what counts as front
    gl::Renderer::SetFrontFaceCW(v.mirrored);
    gl::Renderer::SetClipDistance(0, v.use_clip);
    gl::Renderer::ClearColor(m_clearColor.x, m_clearColor.y, m_clearColor.z, 1.0f);
    gl::Renderer::Clear(true, true);

    m_forward.Bind();
    Mat4 vp = v.proj * v.view;
    m_forward.SetMat4(m_locViewProj, vp.x);
    m_forward.SetVec3(m_locLightDir, m_lightDir.x, m_lightDir.y, m_lightDir.z);
    m_forward.SetVec4(m_locClipPlane, v.clip_plane.x, v.clip_plane.y, v.clip_plane.z,
                      v.clip_plane.w);

    for (const RenderItem& item : m_items)
    {
        const Material* mat = item.material;
        Vec3 color = mat ? mat->base_color : Vec3(0.8f, 0.5f, 0.35f);
        gl::Texture* diffuse = (mat && mat->diffuse) ? mat->diffuse : &m_white;
        diffuse->Bind(0);
        gl::Renderer::SetCull((mat && mat->double_sided) ? gl::CullMode::NONE : gl::CullMode::BACK);
        m_forward.SetFloat(m_locUnlit, (mat && mat->unlit) ? 1.f : 0.f);
        m_forward.SetVec3(m_locColor, color.x, color.y, color.z);
        m_forward.SetMat4(m_locModel, item.world.x);
        item.vao->Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, item.index_count,
                                  item.first_index);
    }
}

void SceneRenderer::draw_water_surfaces(const Mat4& viewProj, const Vec3& cameraPos, float camNear,
                                        float camFar)
{
    m_water.Bind();
    m_water.SetMat4(m_locWViewProj, viewProj.x);
    m_water.SetVec3(m_locWCameraPos, cameraPos.x, cameraPos.y, cameraPos.z);
    m_water.SetVec3(m_locWLightDir, m_lightDir.x, m_lightDir.y, m_lightDir.z);
    m_water.SetVec2(m_locWCamPlanes, camNear, camFar);

    // the surface alpha-fades where it meets the terrain (soft shoreline)
    gl::Renderer::SetBlend(true);
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);

    for (WaterNode* w : m_waters)
    {
        if (!w->quad().is_uploaded()) continue;
        w->reflection_tex().Bind(0);
        w->refraction_tex().Bind(1);
        w->refraction_depth_tex().Bind(2);
        m_water.SetFloat(m_locWTime, w->time() * (w->wave_speed * 40.f));
        m_water.SetVec3(m_locWColor, w->water_color.x, w->water_color.y, w->water_color.z);
        m_water.SetFloat(m_locWDistortion, w->distortion);
        m_water.SetFloat(m_locWTiling, w->wave_tiling);
        m_water.SetFloat(m_locWColorMix, w->color_mix);
        m_water.SetMat4(m_locWModel, w->get_global_transform().x);
        w->quad().vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, w->quad().index_count());
    }
    gl::Renderer::SetBlend(false);
}

void SceneRenderer::render(Scene& scene, int viewport_w, int viewport_h)
{
    if (!m_ready) return;
    Camera3D* camera = scene.get_active_camera();
    if (!camera) return; // nothing to see with

    camera->set_viewport_size(viewport_w, viewport_h);
    Mat4 proj = camera->get_projection_matrix();
    Mat4 view = camera->get_view_matrix();
    Vec3 cameraPos = camera->get_global_position();

    m_waters.clear();
    collect_water(&scene.root(), m_waters);

    // ── extra views: one reflection + one refraction per water surface ──
    for (WaterNode* w : m_waters)
    {
        if (!w->ensure_gpu(viewport_w / 2, viewport_h / 2)) continue;
        const float h = w->surface_height();

        // Reflected camera: mirror the world across the plane y = h
        // (y' = 2h - y), then flip view-space Y so the camera keeps its up
        // vector — equivalent to placing the camera below the surface with
        // its pitch inverted. The two flips cancel: the result is a proper
        // rotation, so triangle winding is unchanged.
        Mat4 mirror = Mat4::Identity();
        mirror.x[5] = -1.f;
        mirror.x[13] = 2.f * h;
        Mat4 flipY = Mat4::Identity();
        flipY.x[5] = -1.f;

        RenderView refl;
        refl.view = flipY * (view * mirror);
        refl.proj = proj;
        refl.target = &w->reflection_fbo();
        refl.w = w->target_width();
        refl.h = w->target_height();
        // keep what is above the surface, plus a small band below it —
        // distorted UVs at the shoreline must never sample clipped (clear
        // color) texels, or the water edge picks up the sky color
        refl.clip_plane = Vec4(0.f, 1.f, 0.f, -h + 0.6f);
        refl.use_clip = true;
        draw_view(scene, refl);

        RenderView refr;
        refr.view = view;
        refr.proj = proj;
        refr.target = &w->refraction_fbo();
        refr.w = w->target_width();
        refr.h = w->target_height();
        // keep what is below the surface, plus a band above it — the shore
        // continues into the refraction texture instead of clipping to sky
        refr.clip_plane = Vec4(0.f, -1.f, 0.f, h + 0.5f);
        refr.use_clip = true;
        draw_view(scene, refr);
    }

    // ── main view ──
    RenderView main_view;
    main_view.view = view;
    main_view.proj = proj;
    main_view.w = viewport_w;
    main_view.h = viewport_h;
    draw_view(scene, main_view);

    // water surfaces draw last, depth-tested against the scene
    if (!m_waters.empty())
        draw_water_surfaces(proj * view, cameraPos, camera->get_near(), camera->get_far());

    if (m_debug_views) draw_debug_views(viewport_w, viewport_h);
}

void SceneRenderer::draw_debug_views(int viewport_w, int viewport_h)
{
    if (m_waters.empty()) return;
    WaterNode* w = m_waters[0];
    if (!w->reflection_tex().IsValid()) return;

    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    m_debug.Bind();
    m_debug.SetVec2(m_locDTargetSize, (float)viewport_w, (float)viewport_h);

    const float dw = viewport_w * 0.28f, dh = viewport_h * 0.28f, pad = 12.f;
    w->reflection_tex().Bind(0);
    m_debug.SetVec4(m_locDRect, pad, pad, dw, dh);
    gl::Renderer::DrawQuad();
    w->refraction_tex().Bind(0);
    m_debug.SetVec4(m_locDRect, viewport_w - dw - pad, pad, dw, dh);
    gl::Renderer::DrawQuad();
    gl::Renderer::SetDepthTest(true);
}
