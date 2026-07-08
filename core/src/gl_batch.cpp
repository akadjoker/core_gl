#include "coregl/gl_batch.hpp"
#include "coregl/gl_renderer.hpp"
#include "gl_platform.hpp"
#include "gl_state.hpp"
#include "gl_font_data.hpp"
#include <cmath>
#include <cstring>

// font atlas layout: 16 columns x 6 rows of 8x8 glyphs (ASCII 32..127)
#define FONT_ATLAS_W 128
#define FONT_ATLAS_H 48
#define FONT_COLS 16

#define K_PI 3.14159265359f
#define K_TAU 6.28318530718f
#define K_DEG2RAD 0.01745329252f

namespace gl
{

#if defined(CORE_GL_ES)
#define BATCH_GLSL_VERSION "#version 300 es\nprecision mediump float;\n"
#else
#define BATCH_GLSL_VERSION "#version 330 core\n"
#endif

static const char* kBatchVS = BATCH_GLSL_VERSION R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform mat4 u_mvp;
out vec2 v_uv;
out vec4 v_color;
void main()
{
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
)";

static const char* kBatchFS = BATCH_GLSL_VERSION R"(
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
out vec4 OutColor;
void main()
{
    OutColor = texture(u_tex, v_uv) * v_color;
}
)";

static inline u32 packColor(u8 r, u8 g, u8 b, u8 a)
{
    return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

// ---------------------------------------------------------------------------
// lifetime
// ---------------------------------------------------------------------------

Batch::Batch()
{
    // identity projection and transform by default
    memset(proj, 0, sizeof(proj));
    proj[0] = proj[5] = proj[10] = proj[15] = 1.f;
    LoadIdentity();
}

Batch::~Batch()
{
    Release();
}

bool Batch::Init(u32 maxVertices)
{
    Release();

    if (!shader.LoadFromString(PipelineStage::VERTEX, kBatchVS) ||
        !shader.LoadFromString(PipelineStage::FRAGMENT, kBatchFS) || !shader.Link())
        return false;
    mvpLoc = shader.GetLocation("u_mvp");
    shader.SetInt("u_tex", 0);

    capacity = maxVertices < 256 ? 256 : maxVertices;
    if (capacity > 65535) capacity = 65535; // u16 index space
    icapacity = capacity * 3;               // worst case: fans (3 indices per vertex)
    verts = new Vertex[capacity];
    indices = new u16[icapacity];
    used = iused = 0;

    cmdCapacity = 1024;
    cmds = new DrawCmd[cmdCapacity];
    cmdCount = 0;
    cmdStart = 0;

    // orphan-friendly storage; layout set once (glBufferData keeps the ids)
    vbo.Allocate(BufferType::ARRAY, nullptr, capacity * sizeof(Vertex), UsageType::STREAM_DRAW);
    const VertexAttrib layout[] = {
        {VertexAttribType::FLOAT, 3, 0, false}, // pos
        {VertexAttribType::FLOAT, 2, 0, false}, // uv
        {VertexAttribType::UBYTE, 4, 0, true},  // color (normalized u32)
    };
    vao.AddVertexBuffer(vbo, layout, 3, sizeof(Vertex));

    ibo.Allocate(BufferType::ELEMENT_ARRAY, nullptr, icapacity * sizeof(u16),
                 UsageType::STREAM_DRAW);
    vao.SetIndexBuffer(ibo, VertexAttribType::USHORT);

    const u8 px[4] = {255, 255, 255, 255};
    white.Load2D(px, 1, 1, TextureFormat::RGBA8);
    curTexture = white.GetHandle();

    // expand the embedded 1-bit font into a white RGBA atlas (alpha = glyph)
    u8* atlas = new u8[FONT_ATLAS_W * FONT_ATLAS_H * 4];
    memset(atlas, 0, FONT_ATLAS_W * FONT_ATLAS_H * 4);
    for (int g = 0; g < 96; ++g)
    {
        int cellX = (g % FONT_COLS) * 8;
        int cellY = (g / FONT_COLS) * 8;
        for (int row = 0; row < 8; ++row)
        {
            u8 bits = kFont8x8[g][row];
            for (int col = 0; col < 8; ++col)
            {
                if (!((bits >> col) & 1)) continue; // LSB = leftmost pixel
                u8* p = &atlas[((cellY + row) * FONT_ATLAS_W + cellX + col) * 4];
                p[0] = p[1] = p[2] = p[3] = 255;
            }
        }
    }
    font.Load2D(atlas, FONT_ATLAS_W, FONT_ATLAS_H, TextureFormat::RGBA8);
    font.SetFilter(TextureFilter::NEAREST, TextureFilter::NEAREST);
    delete[] atlas;

    ready = true;
    return true;
}

void Batch::Release()
{
    shader.Release();
    vbo.Release();
    ibo.Release();
    vao.Release();
    white.Release();
    font.Release();

    delete[] verts;
    delete[] indices;
    delete[] cmds;
    verts = nullptr;
    indices = nullptr;
    cmds = nullptr;
    capacity = used = 0;
    icapacity = iused = 0;
    cmdCapacity = cmdCount = cmdStart = 0;
    ready = false;
}

// ---------------------------------------------------------------------------
// transform stack
// ---------------------------------------------------------------------------

void Batch::SetProjection(const f32* mat4)
{
    memcpy(proj, mat4, sizeof(proj));
}

// xform = xform * m (column-major 4x4)
void Batch::multiply(const f32* m)
{
    f32 r[16];
    for (int c = 0; c < 4; ++c)
    {
        for (int i = 0; i < 4; ++i)
        {
            r[c * 4 + i] = xform[0 * 4 + i] * m[c * 4 + 0] + xform[1 * 4 + i] * m[c * 4 + 1] +
                           xform[2 * 4 + i] * m[c * 4 + 2] + xform[3 * 4 + i] * m[c * 4 + 3];
        }
    }
    memcpy(xform, r, sizeof(r));
    hasTransform = true;
}

void Batch::PushMatrix()
{
    if (stackTop >= kStackDepth) return;
    memcpy(stack[stackTop], xform, sizeof(xform));
    stackActive[stackTop] = hasTransform;
    ++stackTop;
}

void Batch::PopMatrix()
{
    if (stackTop <= 0)
    {
        LoadIdentity();
        return;
    }
    --stackTop;
    memcpy(xform, stack[stackTop], sizeof(xform));
    hasTransform = stackActive[stackTop];
}

void Batch::LoadIdentity()
{
    memset(xform, 0, sizeof(xform));
    xform[0] = xform[5] = xform[10] = xform[15] = 1.f;
    hasTransform = false;
}

void Batch::Translate(f32 x, f32 y, f32 z)
{
    f32 m[16] = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, x, y, z, 1.f};
    multiply(m);
}

void Batch::Rotate(f32 angleDeg, f32 ax, f32 ay, f32 az)
{
    f32 len = sqrtf(ax * ax + ay * ay + az * az);
    if (len <= 0.f) return;
    ax /= len;
    ay /= len;
    az /= len;

    const f32 rad = angleDeg * K_DEG2RAD;
    const f32 c = cosf(rad), s = sinf(rad), t = 1.f - c;

    f32 m[16] = {t * ax * ax + c,
                 t * ax * ay + s * az,
                 t * ax * az - s * ay,
                 0.f,
                 t * ax * ay - s * az,
                 t * ay * ay + c,
                 t * ay * az + s * ax,
                 0.f,
                 t * ax * az + s * ay,
                 t * ay * az - s * ax,
                 t * az * az + c,
                 0.f,
                 0.f,
                 0.f,
                 0.f,
                 1.f};
    multiply(m);
}

void Batch::Scale(f32 x, f32 y, f32 z)
{
    f32 m[16] = {x, 0.f, 0.f, 0.f, 0.f, y, 0.f, 0.f, 0.f, 0.f, z, 0.f, 0.f, 0.f, 0.f, 1.f};
    multiply(m);
}

void Batch::SetTransform(const f32* mat4)
{
    if (mat4)
    {
        memcpy(xform, mat4, sizeof(xform));
        hasTransform = true;
    }
    else
    {
        LoadIdentity();
    }
}

void Batch::applyTransform(Vertex* v, u32 count)
{
    if (!hasTransform || !v) return;
    const f32* m = xform;
    for (u32 i = 0; i < count; ++i)
    {
        const f32 x = v[i].x, y = v[i].y, z = v[i].z;
        v[i].x = m[0] * x + m[4] * y + m[8] * z + m[12];
        v[i].y = m[1] * x + m[5] * y + m[9] * z + m[13];
        v[i].z = m[2] * x + m[6] * y + m[10] * z + m[14];
    }
}

// ---------------------------------------------------------------------------
// command building
// ---------------------------------------------------------------------------

void Batch::closeCmd()
{
    u32 count = iused - cmdStart;
    if (count == 0) return;

    // merge with the previous command when texture+mode match
    if (cmdCount > 0)
    {
        DrawCmd& last = cmds[cmdCount - 1];
        if (last.texture == curTexture && last.mode == curMode &&
            last.start + last.count == cmdStart)
        {
            last.count += count;
            cmdStart = iused;
            return;
        }
    }

    if (cmdCount == cmdCapacity)
    {
        Render(); // out of command slots: flush everything
        return;
    }

    DrawCmd& cmd = cmds[cmdCount++];
    cmd.texture = curTexture;
    cmd.mode = curMode;
    cmd.start = cmdStart;
    cmd.count = count;
    cmdStart = iused;
}

void Batch::SetMode(RenderPrimitive prim)
{
    if (prim == curMode) return;
    closeCmd();
    curMode = prim;
}

void Batch::SetTexture(const Texture* tex)
{
    u32 handle = tex ? tex->GetHandle() : white.GetHandle();
    if (handle == curTexture) return;
    closeCmd();
    curTexture = handle;
}

void Batch::SetColor(u8 r, u8 g, u8 b, u8 a)
{
    curColor = packColor(r, g, b, a);
}

void Batch::SetColorF(f32 r, f32 g, f32 b, f32 a)
{
    curColor = packColor((u8)(r * 255.f), (u8)(g * 255.f), (u8)(b * 255.f), (u8)(a * 255.f));
}

void Batch::TexCoord(f32 u, f32 v)
{
    curU = u;
    curV = v;
}

bool Batch::reserve(u32 vcount, u32 icount, Vertex*& v, u16*& idx, u32& base)
{
    if (!ready || vcount > capacity || icount > icapacity)
    {
        v = nullptr;
        idx = nullptr;
        return false;
    }
    if (used + vcount > capacity || iused + icount > icapacity) Render(); // auto-flush
    v = verts + used;
    idx = indices + iused;
    base = used;
    used += vcount;
    iused += icount;
    return true;
}

// ---------------------------------------------------------------------------
// vertices and 2D shapes
// ---------------------------------------------------------------------------

void Batch::Vertex3(f32 x, f32 y, f32 z)
{
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(1, 1, v, idx, base)) return;
    v[0] = {x, y, z, curU, curV, curColor};
    idx[0] = base;
    applyTransform(v, 1);
}

void Batch::Vertex2(f32 x, f32 y)
{
    Vertex3(x, y, 0.f);
}

void Batch::Line(f32 x0, f32 y0, f32 x1, f32 y1)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    SetMode(RenderPrimitive::LINES);
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(2, 2, v, idx, base)) return;
    v[0] = {x0, y0, 0.f, curU, curV, curColor};
    v[1] = {x1, y1, 0.f, curU, curV, curColor};
    idx[0] = base;
    idx[1] = base + 1;
    applyTransform(v, 2);
}

// writes the standard 4-vertex / 6-index quad topology
static inline void quadIndices(u16* idx, u32 base)
{
    idx[0] = base;
    idx[1] = base + 1;
    idx[2] = base + 2;
    idx[3] = base;
    idx[4] = base + 2;
    idx[5] = base + 3;
}

void Batch::Rect(f32 x, f32 y, f32 w, f32 h, bool fill)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (fill)
    {
        SetMode(RenderPrimitive::TRIANGLES);
        Vertex* v;
        u16* idx;
        u32 base;
        if (!reserve(4, 6, v, idx, base)) return;
        v[0] = {x, y, 0.f, 0.f, 0.f, curColor};
        v[1] = {x, y + h, 0.f, 0.f, 1.f, curColor};
        v[2] = {x + w, y + h, 0.f, 1.f, 1.f, curColor};
        v[3] = {x + w, y, 0.f, 1.f, 0.f, curColor};
        quadIndices(idx, base);
        applyTransform(v, 4);
    }
    else
    {
        SetMode(RenderPrimitive::LINES);
        Vertex* v;
        u16* idx;
        u32 base;
        if (!reserve(4, 8, v, idx, base)) return;
        v[0] = {x, y, 0.f, curU, curV, curColor};
        v[1] = {x + w, y, 0.f, curU, curV, curColor};
        v[2] = {x + w, y + h, 0.f, curU, curV, curColor};
        v[3] = {x, y + h, 0.f, curU, curV, curColor};
        for (u32 i = 0; i < 4; ++i)
        {
            idx[i * 2] = base + i;
            idx[i * 2 + 1] = base + ((i + 1) & 3);
        }
        applyTransform(v, 4);
    }
}

void Batch::Circle(f32 cx, f32 cy, f32 radius, bool fill, int segments)
{
    Ellipse(cx, cy, radius, radius, fill, segments);
}

void Batch::Ellipse(f32 cx, f32 cy, f32 rx, f32 ry, bool fill, int segments)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (segments < 3) segments = 3;
    const f32 step = K_TAU / (f32)segments;
    const u32 n = (u32)segments;

    Vertex* v;
    u16* idx;
    u32 base;
    if (fill)
    {
        // indexed fan: center + ring, ring vertices shared between triangles
        SetMode(RenderPrimitive::TRIANGLES);
        if (!reserve(n + 1, n * 3, v, idx, base)) return;
        v[0] = {cx, cy, 0.f, curU, curV, curColor};
        for (u32 i = 0; i < n; ++i)
        {
            f32 a = step * (f32)i;
            v[i + 1] = {cx + cosf(a) * rx, cy + sinf(a) * ry, 0.f, curU, curV, curColor};
            idx[i * 3] = base;
            idx[i * 3 + 1] = base + 1 + i;
            idx[i * 3 + 2] = base + 1 + ((i + 1) % n);
        }
        applyTransform(v, n + 1);
    }
    else
    {
        SetMode(RenderPrimitive::LINES);
        if (!reserve(n, n * 2, v, idx, base)) return;
        for (u32 i = 0; i < n; ++i)
        {
            f32 a = step * (f32)i;
            v[i] = {cx + cosf(a) * rx, cy + sinf(a) * ry, 0.f, curU, curV, curColor};
            idx[i * 2] = base + i;
            idx[i * 2 + 1] = base + ((i + 1) % n);
        }
        applyTransform(v, n);
    }
}

void Batch::Triangle(f32 x1, f32 y1, f32 x2, f32 y2, f32 x3, f32 y3, bool fill)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    Vertex* v;
    u16* idx;
    u32 base;
    if (fill)
    {
        SetMode(RenderPrimitive::TRIANGLES);
        if (!reserve(3, 3, v, idx, base)) return;
        idx[0] = base;
        idx[1] = base + 1;
        idx[2] = base + 2;
    }
    else
    {
        SetMode(RenderPrimitive::LINES);
        if (!reserve(3, 6, v, idx, base)) return;
        idx[0] = base;
        idx[1] = base + 1;
        idx[2] = base + 1;
        idx[3] = base + 2;
        idx[4] = base + 2;
        idx[5] = base;
    }
    v[0] = {x1, y1, 0.f, curU, curV, curColor};
    v[1] = {x2, y2, 0.f, curU, curV, curColor};
    v[2] = {x3, y3, 0.f, curU, curV, curColor};
    applyTransform(v, 3);
}

void Batch::Ring(f32 cx, f32 cy, f32 rInner, f32 rOuter, bool fill, int segments)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (segments < 3) segments = 3;
    const u32 n = (u32)segments;
    const f32 step = K_TAU / (f32)segments;

    if (fill)
    {
        // vertex pairs (inner, outer) shared between segments
        SetMode(RenderPrimitive::TRIANGLES);
        Vertex* v;
        u16* idx;
        u32 base;
        if (!reserve(n * 2, n * 6, v, idx, base)) return;
        for (u32 i = 0; i < n; ++i)
        {
            f32 a = step * (f32)i;
            f32 c = cosf(a), s = sinf(a);
            v[i * 2] = {cx + c * rInner, cy + s * rInner, 0.f, curU, curV, curColor};
            v[i * 2 + 1] = {cx + c * rOuter, cy + s * rOuter, 0.f, curU, curV, curColor};

            u32 i0 = base + i * 2, o0 = i0 + 1;
            u32 i1 = base + ((i + 1) % n) * 2, o1 = i1 + 1;
            idx[i * 6] = i0;
            idx[i * 6 + 1] = o0;
            idx[i * 6 + 2] = o1;
            idx[i * 6 + 3] = i0;
            idx[i * 6 + 4] = o1;
            idx[i * 6 + 5] = i1;
        }
        applyTransform(v, n * 2);
    }
    else
    {
        Circle(cx, cy, rInner, false, segments);
        Circle(cx, cy, rOuter, false, segments);
    }
}

void Batch::Arc(f32 cx, f32 cy, f32 radius, f32 startDeg, f32 endDeg, int segments)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (segments < 1) segments = 1;
    const u32 n = (u32)segments;
    const f32 a0 = startDeg * K_DEG2RAD;
    const f32 step = (endDeg - startDeg) * K_DEG2RAD / (f32)segments;

    SetMode(RenderPrimitive::LINES);
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(n + 1, n * 2, v, idx, base)) return;
    for (u32 i = 0; i <= n; ++i)
    {
        f32 a = a0 + step * (f32)i;
        v[i] = {cx + cosf(a) * radius, cy + sinf(a) * radius, 0.f, curU, curV, curColor};
    }
    for (u32 i = 0; i < n; ++i)
    {
        idx[i * 2] = base + i;
        idx[i * 2 + 1] = base + i + 1;
    }
    applyTransform(v, n + 1);
}

void Batch::ThickLine(f32 x0, f32 y0, f32 x1, f32 y1, f32 thickness)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    f32 dx = x1 - x0, dy = y1 - y0;
    f32 len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.f) return;
    const f32 nx = -dy / len * thickness * 0.5f;
    const f32 ny = dx / len * thickness * 0.5f;

    SetMode(RenderPrimitive::TRIANGLES);
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(4, 6, v, idx, base)) return;
    v[0] = {x0 + nx, y0 + ny, 0.f, curU, curV, curColor};
    v[1] = {x0 - nx, y0 - ny, 0.f, curU, curV, curColor};
    v[2] = {x1 - nx, y1 - ny, 0.f, curU, curV, curColor};
    v[3] = {x1 + nx, y1 + ny, 0.f, curU, curV, curColor};
    quadIndices(idx, base);
    applyTransform(v, 4);
}

void Batch::Polygon(f32 cx, f32 cy, int sides, f32 radius, f32 rotationDeg, bool fill)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (sides < 3) sides = 3;
    const u32 n = (u32)sides;
    const f32 rot = rotationDeg * K_DEG2RAD;
    const f32 step = K_TAU / (f32)sides;

    Vertex* v;
    u16* idx;
    u32 base;
    if (fill)
    {
        SetMode(RenderPrimitive::TRIANGLES);
        if (!reserve(n + 1, n * 3, v, idx, base)) return;
        v[0] = {cx, cy, 0.f, curU, curV, curColor};
        for (u32 i = 0; i < n; ++i)
        {
            f32 a = rot + step * (f32)i;
            v[i + 1] = {cx + cosf(a) * radius, cy + sinf(a) * radius, 0.f, curU, curV, curColor};
            idx[i * 3] = base;
            idx[i * 3 + 1] = base + 1 + i;
            idx[i * 3 + 2] = base + 1 + ((i + 1) % n);
        }
        applyTransform(v, n + 1);
    }
    else
    {
        SetMode(RenderPrimitive::LINES);
        if (!reserve(n, n * 2, v, idx, base)) return;
        for (u32 i = 0; i < n; ++i)
        {
            f32 a = rot + step * (f32)i;
            v[i] = {cx + cosf(a) * radius, cy + sinf(a) * radius, 0.f, curU, curV, curColor};
            idx[i * 2] = base + i;
            idx[i * 2 + 1] = base + ((i + 1) % n);
        }
        applyTransform(v, n);
    }
}

void Batch::Polyline(const f32* xyPairs, int pointCount)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (!xyPairs || pointCount < 2) return;
    const u32 n = (u32)pointCount;

    SetMode(RenderPrimitive::LINES);
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(n, (n - 1) * 2, v, idx, base)) return;
    for (u32 i = 0; i < n; ++i)
        v[i] = {xyPairs[i * 2], xyPairs[i * 2 + 1], 0.f, curU, curV, curColor};
    for (u32 i = 0; i < n - 1; ++i)
    {
        idx[i * 2] = base + i;
        idx[i * 2 + 1] = base + i + 1;
    }
    applyTransform(v, n);
}

void Batch::Grid(f32 x, f32 y, f32 w, f32 h, f32 cellW, f32 cellH)
{
    if (cellW <= 0.f || cellH <= 0.f) return;
    for (f32 gx = x; gx <= x + w + 0.001f; gx += cellW)
        Line(gx, y, gx, y + h);
    for (f32 gy = y; gy <= y + h + 0.001f; gy += cellH)
        Line(x, gy, x + w, gy);
}

// ---------------------------------------------------------------------------
// 3D wireframe shapes
// ---------------------------------------------------------------------------

void Batch::CubeWire(f32 cx, f32 cy, f32 cz, f32 sx, f32 sy, f32 sz)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    const f32 hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;

    SetMode(RenderPrimitive::LINES);
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(8, 24, v, idx, base)) return; // 8 corners, 12 edges

    for (int i = 0; i < 8; ++i)
    {
        v[i] = {cx + ((i & 1) ? hx : -hx),
                cy + ((i & 2) ? hy : -hy),
                cz + ((i & 4) ? hz : -hz),
                curU,
                curV,
                curColor};
    }
    static const u8 kEdges[24] = {0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7,
                                  7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7};
    for (int i = 0; i < 24; ++i)
        idx[i] = base + kEdges[i];
    applyTransform(v, 8);
}

void Batch::SphereWire(f32 cx, f32 cy, f32 cz, f32 radius, int rings, int slices)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (rings < 2) rings = 2;
    if (slices < 3) slices = 3;
    const u32 nr = (u32)rings, ns = (u32)slices;

    SetMode(RenderPrimitive::LINES);
    // shared grid: (rings+1) rows x slices columns (poles duplicated per column)
    const u32 vcount = (nr + 1) * ns;
    const u32 icount = (nr - 1) * ns * 2 + nr * ns * 2; // latitude + longitude segments
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(vcount, icount, v, idx, base)) return;

    for (u32 r = 0; r <= nr; ++r)
    {
        f32 phi = K_PI * (f32)r / (f32)nr;
        f32 y = cy + cosf(phi) * radius;
        f32 rad = sinf(phi) * radius;
        for (u32 s = 0; s < ns; ++s)
        {
            f32 a = K_TAU * (f32)s / (f32)ns;
            v[r * ns + s] = {cx + cosf(a) * rad, y, cz + sinf(a) * rad, curU, curV, curColor};
        }
    }
    u16* w = idx;
    for (u32 r = 1; r < nr; ++r) // latitude circles
    {
        for (u32 s = 0; s < ns; ++s)
        {
            *w++ = base + r * ns + s;
            *w++ = base + r * ns + ((s + 1) % ns);
        }
    }
    for (u32 s = 0; s < ns; ++s) // longitude lines
    {
        for (u32 r = 0; r < nr; ++r)
        {
            *w++ = base + r * ns + s;
            *w++ = base + (r + 1) * ns + s;
        }
    }
    applyTransform(v, vcount);
}

void Batch::CylinderWire(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int slices)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (slices < 4) slices = 4;
    const u32 ns = (u32)slices;
    const f32 y0 = cy - height * 0.5f, y1 = cy + height * 0.5f;

    SetMode(RenderPrimitive::LINES);
    const u32 vcount = ns * 2;             // bottom ring + top ring
    const u32 icount = ns * 2 * 2 + 4 * 2; // two circles + 4 verticals
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(vcount, icount, v, idx, base)) return;

    for (u32 s = 0; s < ns; ++s)
    {
        f32 a = K_TAU * (f32)s / (f32)ns;
        f32 px = cx + cosf(a) * radius, pz = cz + sinf(a) * radius;
        v[s] = {px, y0, pz, curU, curV, curColor};
        v[ns + s] = {px, y1, pz, curU, curV, curColor};
    }
    u16* w = idx;
    for (u32 s = 0; s < ns; ++s)
    {
        *w++ = base + s;
        *w++ = base + ((s + 1) % ns);
        *w++ = base + ns + s;
        *w++ = base + ns + ((s + 1) % ns);
    }
    for (u32 k = 0; k < 4; ++k) // verticals reuse the ring vertices
    {
        u32 s = k * ns / 4;
        *w++ = base + s;
        *w++ = base + ns + s;
    }
    applyTransform(v, vcount);
}

void Batch::CapsuleWire(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int slices)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (slices < 4) slices = 4;
    const u32 ns = (u32)slices;
    const u32 arcs = ns / 2;
    f32 half = height * 0.5f - radius; // cylinder half-length
    if (half < 0.f) half = 0.f;
    const f32 yTop = cy + half, yBot = cy - half;

    SetMode(RenderPrimitive::LINES);
    // rings (2*ns) + 4 arcs per cap, each with arcs+1 shared vertices
    const u32 arcVerts = 8 * (arcs + 1);
    const u32 vcount = ns * 2 + arcVerts;
    const u32 icount = ns * 2 * 2 + 4 * 2 + 8 * arcs * 2;
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(vcount, icount, v, idx, base)) return;

    for (u32 s = 0; s < ns; ++s)
    {
        f32 a = K_TAU * (f32)s / (f32)ns;
        f32 px = cx + cosf(a) * radius, pz = cz + sinf(a) * radius;
        v[s] = {px, yBot, pz, curU, curV, curColor};
        v[ns + s] = {px, yTop, pz, curU, curV, curColor};
    }
    u16* w = idx;
    for (u32 s = 0; s < ns; ++s) // the two rings
    {
        *w++ = base + s;
        *w++ = base + ((s + 1) % ns);
        *w++ = base + ns + s;
        *w++ = base + ns + ((s + 1) % ns);
    }
    for (u32 k = 0; k < 4; ++k) // verticals
    {
        u32 s = k * ns / 4;
        *w++ = base + s;
        *w++ = base + ns + s;
    }
    // hemisphere arcs: 4 around the top cap + 4 around the bottom cap
    u32 vi = ns * 2;
    for (u32 k = 0; k < 4; ++k)
    {
        f32 theta = K_TAU * (f32)k / 4.f;
        f32 ct = cosf(theta), st = sinf(theta);
        for (int cap = 0; cap < 2; ++cap)
        {
            f32 yBase = cap ? yBot : yTop;
            f32 sign = cap ? -1.f : 1.f;
            u32 first = vi;
            for (u32 i = 0; i <= arcs; ++i)
            {
                f32 p = K_PI * 0.5f * (f32)i / (f32)arcs;
                v[vi++] = {cx + cosf(p) * ct * radius,
                           yBase + sign * sinf(p) * radius,
                           cz + cosf(p) * st * radius,
                           curU,
                           curV,
                           curColor};
            }
            for (u32 i = 0; i < arcs; ++i)
            {
                *w++ = base + first + i;
                *w++ = base + first + i + 1;
            }
        }
    }
    applyTransform(v, vcount);
}

void Batch::Grid3D(f32 size, f32 step)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (step <= 0.f) return;
    SetMode(RenderPrimitive::LINES);
    const f32 half = size * 0.5f;
    for (f32 p = -half; p <= half + 0.001f; p += step)
    {
        Vertex* v;
        u16* idx;
        u32 base;
        if (!reserve(4, 4, v, idx, base)) return;
        v[0] = {p, 0.f, -half, curU, curV, curColor};
        v[1] = {p, 0.f, half, curU, curV, curColor};
        v[2] = {-half, 0.f, p, curU, curV, curColor};
        v[3] = {half, 0.f, p, curU, curV, curColor};
        for (u32 i = 0; i < 4; ++i)
            idx[i] = base + i;
        applyTransform(v, 4);
    }
}

void Batch::Axes(f32 size)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    SetMode(RenderPrimitive::LINES);
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(6, 6, v, idx, base)) return;
    v[0] = {0.f, 0.f, 0.f, curU, curV, packColor(230, 60, 60, 255)};
    v[1] = {size, 0.f, 0.f, curU, curV, packColor(230, 60, 60, 255)};
    v[2] = {0.f, 0.f, 0.f, curU, curV, packColor(60, 200, 60, 255)};
    v[3] = {0.f, size, 0.f, curU, curV, packColor(60, 200, 60, 255)};
    v[4] = {0.f, 0.f, 0.f, curU, curV, packColor(70, 110, 240, 255)};
    v[5] = {0.f, 0.f, size, curU, curV, packColor(70, 110, 240, 255)};
    for (u32 i = 0; i < 6; ++i)
        idx[i] = base + i;
    applyTransform(v, 6);
}

// ---------------------------------------------------------------------------
// 3D solid shapes (flat current color)
// ---------------------------------------------------------------------------

void Batch::Cube(f32 cx, f32 cy, f32 cz, f32 sx, f32 sy, f32 sz)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    const f32 hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;

    SetMode(RenderPrimitive::TRIANGLES);
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(8, 36, v, idx, base)) return; // 8 shared corners, 12 triangles

    for (int i = 0; i < 8; ++i)
    {
        v[i] = {cx + ((i & 1) ? hx : -hx),
                cy + ((i & 2) ? hy : -hy),
                cz + ((i & 4) ? hz : -hz),
                curU,
                curV,
                curColor};
    }
    static const u8 kFaces[36] = {
        0, 2, 3, 0, 3, 1, // -z
        4, 5, 7, 4, 7, 6, // +z
        0, 4, 6, 0, 6, 2, // -x
        1, 3, 7, 1, 7, 5, // +x
        0, 1, 5, 0, 5, 4, // -y
        2, 6, 7, 2, 7, 3, // +y
    };
    for (int i = 0; i < 36; ++i)
        idx[i] = base + kFaces[i];
    applyTransform(v, 8);
}

void Batch::Sphere(f32 cx, f32 cy, f32 cz, f32 radius, int rings, int slices)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (rings < 2) rings = 2;
    if (slices < 3) slices = 3;
    const u32 nr = (u32)rings, ns = (u32)slices;

    SetMode(RenderPrimitive::TRIANGLES);
    const u32 vcount = (nr + 1) * ns;
    const u32 icount = nr * ns * 6;
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(vcount, icount, v, idx, base)) return;

    for (u32 r = 0; r <= nr; ++r)
    {
        f32 phi = K_PI * (f32)r / (f32)nr;
        f32 y = cy + cosf(phi) * radius;
        f32 rad = sinf(phi) * radius;
        for (u32 s = 0; s < ns; ++s)
        {
            f32 a = K_TAU * (f32)s / (f32)ns;
            v[r * ns + s] = {cx + cosf(a) * rad, y, cz + sinf(a) * rad, curU, curV, curColor};
        }
    }
    u16* w = idx;
    for (u32 r = 0; r < nr; ++r)
    {
        for (u32 s = 0; s < ns; ++s)
        {
            u32 s1 = (s + 1) % ns;
            u32 a = base + r * ns + s, b = base + r * ns + s1;
            u32 c = base + (r + 1) * ns + s, d = base + (r + 1) * ns + s1;
            *w++ = a;
            *w++ = c;
            *w++ = d;
            *w++ = a;
            *w++ = d;
            *w++ = b;
        }
    }
    applyTransform(v, vcount);
}

void Batch::Cylinder(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int slices)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (slices < 3) slices = 3;
    const u32 ns = (u32)slices;
    const f32 y0 = cy - height * 0.5f, y1 = cy + height * 0.5f;

    SetMode(RenderPrimitive::TRIANGLES);
    // two rings + two cap centers; side quads + cap fans reuse the rings
    const u32 vcount = ns * 2 + 2;
    const u32 icount = ns * 6 + ns * 3 * 2;
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(vcount, icount, v, idx, base)) return;

    for (u32 s = 0; s < ns; ++s)
    {
        f32 a = K_TAU * (f32)s / (f32)ns;
        f32 px = cx + cosf(a) * radius, pz = cz + sinf(a) * radius;
        v[s] = {px, y0, pz, curU, curV, curColor};
        v[ns + s] = {px, y1, pz, curU, curV, curColor};
    }
    const u32 cBot = ns * 2, cTop = ns * 2 + 1;
    v[cBot] = {cx, y0, cz, curU, curV, curColor};
    v[cTop] = {cx, y1, cz, curU, curV, curColor};

    u16* w = idx;
    for (u32 s = 0; s < ns; ++s)
    {
        u32 s1 = (s + 1) % ns;
        u32 b0 = base + s, b1 = base + s1;
        u32 t0 = base + ns + s, t1 = base + ns + s1;
        // side quad
        *w++ = b0;
        *w++ = t0;
        *w++ = t1;
        *w++ = b0;
        *w++ = t1;
        *w++ = b1;
        // caps
        *w++ = base + cBot;
        *w++ = b1;
        *w++ = b0;
        *w++ = base + cTop;
        *w++ = t0;
        *w++ = t1;
    }
    applyTransform(v, vcount);
}

void Batch::Capsule(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int rings, int slices)
{
    SetTexture(nullptr); // plain shapes always use the white texture
    if (rings < 1) rings = 1;
    if (slices < 3) slices = 3;
    const u32 nr = (u32)rings, ns = (u32)slices;
    f32 half = height * 0.5f - radius; // cylinder half-length
    if (half < 0.f) half = 0.f;

    SetMode(RenderPrimitive::TRIANGLES);
    // sphere grid split at the equator: rows 0..nr = top hemisphere (+half),
    // rows nr+1..2nr+1 = bottom hemisphere (-half); side quads bridge the split
    const u32 rows = 2 * nr + 2;
    const u32 vcount = rows * ns;
    const u32 icount = (rows - 1) * ns * 6;
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(vcount, icount, v, idx, base)) return;

    for (u32 r = 0; r < rows; ++r)
    {
        bool top = r <= nr;
        u32 hr = top ? r : r - nr - 1; // row within the hemisphere
        f32 phi = K_PI * 0.5f * (top ? (f32)hr / (f32)nr : 1.f + (f32)hr / (f32)nr);
        f32 y = cy + (top ? half : -half) + cosf(phi) * radius;
        f32 rad = sinf(phi) * radius;
        for (u32 s = 0; s < ns; ++s)
        {
            f32 a = K_TAU * (f32)s / (f32)ns;
            v[r * ns + s] = {cx + cosf(a) * rad, y, cz + sinf(a) * rad, curU, curV, curColor};
        }
    }
    u16* w = idx;
    for (u32 r = 0; r + 1 < rows; ++r)
    {
        for (u32 s = 0; s < ns; ++s)
        {
            u32 s1 = (s + 1) % ns;
            u32 a = base + r * ns + s, b = base + r * ns + s1;
            u32 c = base + (r + 1) * ns + s, d = base + (r + 1) * ns + s1;
            *w++ = a;
            *w++ = c;
            *w++ = d;
            *w++ = a;
            *w++ = d;
            *w++ = b;
        }
    }
    applyTransform(v, vcount);
}

// ---------------------------------------------------------------------------
// textured quads and text
// ---------------------------------------------------------------------------

void Batch::Quad(const Texture& tex, f32 x, f32 y, f32 w, f32 h)
{
    SetTexture(&tex);
    SetMode(RenderPrimitive::TRIANGLES);
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(4, 6, v, idx, base)) return;
    v[0] = {x, y, 0.f, 0.f, 0.f, curColor};
    v[1] = {x, y + h, 0.f, 0.f, 1.f, curColor};
    v[2] = {x + w, y + h, 0.f, 1.f, 1.f, curColor};
    v[3] = {x + w, y, 0.f, 1.f, 0.f, curColor};
    quadIndices(idx, base);
    applyTransform(v, 4);
}

void Batch::Quad(const Texture& tex, f32 srcX, f32 srcY, f32 srcW, f32 srcH, f32 x, f32 y, f32 w,
                 f32 h)
{
    const f32 tw = (f32)tex.GetWidth();
    const f32 th = (f32)tex.GetHeight();
    if (tw <= 0.f || th <= 0.f) return;
    const f32 u0 = srcX / tw, v0 = srcY / th;
    const f32 u1 = (srcX + srcW) / tw, v1 = (srcY + srcH) / th;

    SetTexture(&tex);
    SetMode(RenderPrimitive::TRIANGLES);
    Vertex* v;
    u16* idx;
    u32 base;
    if (!reserve(4, 6, v, idx, base)) return;
    v[0] = {x, y, 0.f, u0, v0, curColor};
    v[1] = {x, y + h, 0.f, u0, v1, curColor};
    v[2] = {x + w, y + h, 0.f, u1, v1, curColor};
    v[3] = {x + w, y, 0.f, u1, v0, curColor};
    quadIndices(idx, base);
    applyTransform(v, 4);
}

void Batch::Text(f32 x, f32 y, f32 size, const char* text)
{
    if (!text || size <= 0.f) return;

    SetTexture(&font);
    SetMode(RenderPrimitive::TRIANGLES);

    const f32 cw = 8.f / (f32)FONT_ATLAS_W; // glyph cell size in UV space
    const f32 ch = 8.f / (f32)FONT_ATLAS_H;
    f32 penX = x, penY = y;

    for (const char* c = text; *c; ++c)
    {
        if (*c == '\n')
        {
            penX = x;
            penY += size;
            continue;
        }
        u8 code = (u8)*c;
        if (code < 32 || code > 127) code = '?';
        if (code != ' ')
        {
            int g = code - 32;
            const f32 u0 = (f32)(g % FONT_COLS) * cw;
            const f32 v0 = (f32)(g / FONT_COLS) * ch;
            const f32 u1 = u0 + cw;
            const f32 v1 = v0 + ch;

            Vertex* v;
            u16* idx;
            u32 base;
            if (!reserve(4, 6, v, idx, base)) return;
            v[0] = {penX, penY, 0.f, u0, v0, curColor};
            v[1] = {penX, penY + size, 0.f, u0, v1, curColor};
            v[2] = {penX + size, penY + size, 0.f, u1, v1, curColor};
            v[3] = {penX + size, penY, 0.f, u1, v0, curColor};
            quadIndices(idx, base);
            applyTransform(v, 4);
        }
        penX += size; // monospace: the 8x8 glyphs include their own spacing
    }
}

f32 Batch::TextWidth(f32 size, const char* text) const
{
    if (!text) return 0.f;
    u32 longest = 0, line = 0;
    for (const char* c = text; *c; ++c)
    {
        if (*c == '\n')
        {
            if (line > longest) longest = line;
            line = 0;
            continue;
        }
        ++line;
    }
    if (line > longest) longest = line;
    return (f32)longest * size;
}

// ---------------------------------------------------------------------------
// flush
// ---------------------------------------------------------------------------

void Batch::Render()
{
    if (!ready) return;
    closeCmd();
    if (cmdCount == 0)
    {
        used = iused = 0;
        cmdStart = 0;
        return;
    }

    shader.Bind();
    shader.SetMat4(mvpLoc, proj);
    vao.Bind();

    // orphan + upload both buffers in one call each: glBufferData with fresh
    // data never stalls on frames the GPU is still reading the old contents
    vbo.Allocate(BufferType::ARRAY, verts, used * sizeof(Vertex), UsageType::STREAM_DRAW);
    ibo.Allocate(BufferType::ELEMENT_ARRAY, indices, iused * sizeof(u16), UsageType::STREAM_DRAW);

    for (u32 i = 0; i < cmdCount; ++i)
    {
        const DrawCmd& cmd = cmds[i];
        state::BindTexture(0, GL_TEXTURE_2D, cmd.texture);
        Renderer::DrawIndexed(cmd.mode, cmd.count, cmd.start);
    }

    used = iused = 0;
    cmdCount = 0;
    cmdStart = 0;
}

} // namespace gl
