#include "scene/LensFlareNode.hpp"
#include "scene/Camera3D.hpp"
#include "scene/Pixmap.hpp"
#include "flares_png.h"
#include <coregl/gl_renderer.hpp>
#include <algorithm>
#include <cmath>
#include <string>

// version-less shaders; ShaderHeader is prepended at compile time
// Vertex layout: pos=0(float3), uv=1(float2), color=2(float4) — simple, no MeshVertex
static const char* kLensVS = R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
out vec2 v_uv;
out vec4 v_col;
void main() {
    v_uv = a_uv;
    v_col = a_col;
    gl_Position = vec4(a_pos, 1.0);
}
)";

static const char* kLensFS = R"(
in vec2 v_uv;
in vec4 v_col;
uniform sampler2D u_tex;
out vec4 frag;
void main() {
    vec4 t = texture(u_tex, v_uv);
    frag = vec4(t.rgb * v_col.rgb, t.a * v_col.a);
}
)";

static bool loadStage(gl::Shader& shader, gl::PipelineStage stage, const char* body)
{
    std::string src = std::string(gl::Renderer::ShaderHeader(stage)) + body;
    return shader.LoadFromString(stage, src.c_str());
}

// ── predefined flares ──
void LensFlareNode::init_default_flares()
{
    m_flares.clear();
    struct Clip { float x, y, w, h; };
 
 
    static const Clip clips[] = {
        {  1,   1, 127, 127},   // 0 estrela
        {  1, 128, 127, 127},   // 1 sol
        {128,   1,  62,  62},   // 2 halo
        {128,   128,  62,  62},   // 3 halo cheio
        {128,  64,  63,  63},   // 4 círculo
        {128, 132,  63,  60},   // 5 círculo cheio pequeno
        {128, 192,  63,  63},   // 6 hexágono
    };
    struct Def { int clip; float pos, size; Vec3 color; float alpha; };
    static const Def defs[] = {
        {2,  0.60f, 0.08f, {0.8f, 0.8f, 1.0f}, 0.7f},
        {4,  0.45f, 0.09f, {1.0f, 0.9f, 1.0f}, 0.6f},
        {3,  0.25f, 0.06f, {0.6f, 1.0f, 0.6f}, 0.5f},
        {4,  0.00f, 0.12f, {1.0f, 0.95f,0.85f},1.0f},
         {5, 1.20f, 0.08f, {1.0f, 0.8f, 0.6f}, 0.5f},
         {6, 1.40f, 0.10f, {0.7f, 0.9f, 1.0f}, 0.4f},
         {2, 1.60f, 0.15f, {0.7f, 0.9f, 1.0f}, 0.4f},
    };
    for (const auto& d : defs)
    {
        const Clip& c = clips[d.clip];
        Element e;
        e.position = d.pos; e.size = d.size; e.color = d.color; e.alpha = d.alpha;
        e.pixelRect = Vec4(c.x, c.y, c.w, c.h);
        m_flares.push_back(e);
    }
}

float LensFlareNode::computeFade(const Vec2& ndc) const
{
    float mx = std::max(std::fabs(ndc.x), std::fabs(ndc.y));
    if (mx > 1.3f) return 0.f;
    if (mx < 0.7f) return 1.f;
    return 1.f - (mx - 0.7f) / 0.6f;
}

LensFlareNode::LensFlareNode(const std::string& name)
    : Node3D(name)
{
    m_type = NT_LENSFLARE;
    init_default_flares();
}

LensFlareNode::~LensFlareNode() { release_gpu(); }

bool LensFlareNode::ensure_gpu()
{
    if (m_gpuReady) return true;

    // Load flare atlas texture on first use
    if (!m_texLoaded)
    {
        scene::Pixmap pm;
        if (pm.load_from_memory(kFlarePng, kFlarePngLen))
        {
            m_tex.Load2D(pm.pixels, (int)pm.width, (int)pm.height, gl::TextureFormat::RGBA8);
            m_tex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
            m_tex.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
        }
        else
        {
            m_tex.Load2D(nullptr, 1, 1, gl::TextureFormat::RGBA8);
        }
        m_texLoaded = true;
    }

    if (!m_shader)
    {
        m_shader = new gl::Shader();
        if (!loadStage(*m_shader, gl::PipelineStage::VERTEX, kLensVS) ||
            !loadStage(*m_shader, gl::PipelineStage::FRAGMENT, kLensFS) ||
            !m_shader->Link())
        {
            delete m_shader; m_shader = nullptr; return false;
        }
    }

    // allocate for max quads (4 verts, 6 indices each)
    int maxQuads = (int)m_flares.size();
    if (maxQuads < 1) maxQuads = 8;
    m_capacity = (u32)maxQuads;

    int maxVerts = maxQuads * 4;
    int maxIdx   = maxQuads * 6;

    // build static index buffer
    m_scratchIdx.resize((size_t)maxIdx);
    for (int q = 0; q < maxQuads; ++q)
    {
        u32 base = (u32)(q * 4);
        u32* tri = &m_scratchIdx[(size_t)q * 6];
        tri[0] = base; tri[1] = base + 1; tri[2] = base + 2;
        tri[3] = base + 2; tri[4] = base + 3; tri[5] = base;
    }

    m_vbo.Allocate(gl::BufferType::ARRAY, nullptr, (size_t)maxVerts * sizeof(FVert),
                   gl::UsageType::DYNAMIC_DRAW);
    m_ibo.Allocate(gl::BufferType::ELEMENT_ARRAY, m_scratchIdx.data(),
                   m_scratchIdx.size() * sizeof(u32), gl::UsageType::STATIC_DRAW);

    // vertex layout: pos=0(float3), uv=1(float2), color=2(float4)
    // offset is computed automatically by AddVertexBuffer; stride = sizeof(FVert)
    const gl::VertexAttrib layout[] = {
        {gl::VertexAttribType::FLOAT, 3, 0, false},
        {gl::VertexAttribType::FLOAT, 2, 0, false},
        {gl::VertexAttribType::FLOAT, 4, 0, false},
    };
    m_vao.AddVertexBuffer(m_vbo, layout, 3, sizeof(FVert));
    m_vao.SetIndexBuffer(m_ibo, gl::VertexAttribType::UINT);

    m_scratchVerts.resize((size_t)maxVerts);
    m_gpuReady = true;
    return true;
}

void LensFlareNode::release_gpu()
{
    delete m_shader; m_shader = nullptr;
    m_vao.Release();
    m_vbo.Release();
    m_ibo.Release();
    m_tex.Release();
    m_texLoaded = false;
    m_gpuReady = false;
}

void LensFlareNode::buildGeometry(const Vec2& sunNDC, float fade, float aspect)
{
    int q = 0;
    for (const auto& fe : m_flares)
    {
        Vec2 centre(sunNDC.x * fe.position, sunNDC.y * fe.position);
        float hH = fe.size, hW = hH / aspect;
        float a = fe.alpha * fade;

        float u0, v0, u1, v1;
        if (fe.pixelRect.z <= 0.f) { u0 = 0.f; v0 = 0.f; u1 = 1.f; v1 = 1.f; }
        else
        {
            u0 = fe.pixelRect.x / m_atlasW; u1 = (fe.pixelRect.x + fe.pixelRect.z) / m_atlasW;
            // PNG is top-down; invert V so OpenGL bottom-left origin maps correctly
            v0 = 1.0f - (fe.pixelRect.y + fe.pixelRect.w) / m_atlasH; // PNG bottom → GL bottom
            v1 = 1.0f - fe.pixelRect.y / m_atlasH;                      // PNG top → GL top
        }

        FVert* fv = &m_scratchVerts[(size_t)q * 4];
        // bottom-left
        fv[0] = {centre.x - hW, centre.y - hH, 0.999f, u0, v0, fe.color.x, fe.color.y, fe.color.z, a};
        // bottom-right
        fv[1] = {centre.x + hW, centre.y - hH, 0.999f, u1, v0, fe.color.x, fe.color.y, fe.color.z, a};
        // top-right
        fv[2] = {centre.x + hW, centre.y + hH, 0.999f, u1, v1, fe.color.x, fe.color.y, fe.color.z, a};
        // top-left
        fv[3] = {centre.x - hW, centre.y + hH, 0.999f, u0, v1, fe.color.x, fe.color.y, fe.color.z, a};
        ++q;
    }
    m_indexCount = (u32)(q * 6);
    m_vbo.Upload(m_scratchVerts.data(), m_scratchVerts.size() * sizeof(FVert));
}

u32 LensFlareNode::build(Camera3D& cam, const Vec3& sunDir)
{
    if (!m_enabled || m_flares.empty()) return 0;
    if (!m_gpuReady && !ensure_gpu()) return 0;
    if (!m_shader) return 0;

    Vec3 camPos = cam.get_global_position();
    Vec3 sunWorld = camPos + sunDir * 5000.f;
    Mat4 vp = cam.get_projection_matrix() * cam.get_view_matrix();
    Vec4 clip = vp * Vec4(sunWorld.x, sunWorld.y, sunWorld.z, 1.f);
    if (clip.w <= 0.f) return 0;

    Vec2 sunNDC(clip.x / clip.w, clip.y / clip.w);
    float fade = computeFade(sunNDC);

    // sun elevation: fade out as it nears the horizon, gone once below
    // (sunDir points AT the sun, so .y is sin(elevation))
    float elev = sunDir.y / 0.08f;
    fade *= elev < 0.f ? 0.f : (elev > 1.f ? 1.f : elev);
    if (fade <= 0.f) return 0;

    buildGeometry(sunNDC, fade, cam.get_aspect());
    return m_indexCount;
}
