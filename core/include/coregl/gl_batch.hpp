#pragma once

#include "gl_types.hpp"
#include "gl_shader.hpp"
#include "gl_buffer.hpp"
#include "gl_vertex_array.hpp"
#include "gl_texture.hpp"

namespace gl
{

// Immediate-mode 2D/3D batcher (optimized internals):
// - 24-byte vertex (pos3f + uv2f + color as packed u32, normalized by the GPU)
// - fully indexed: quads are 4 verts + 6 indices, circle fills are fans
// - VBO and IBO orphaned per Render() — no driver sync stalls
// - consecutive draws with the same texture+mode are merged into one call
// - the projection is a raw 16-float column-major array

class Batch
{
public:
    Batch();
    ~Batch();
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;

    // maxVertices is the flush threshold; auto-flushes when full
    bool Init(u32 maxVertices = 8192);
    void Release();

    // column-major 16 floats (ortho, perspective, whatever); applied at Render
    void SetProjection(const f32* mat4);

    // Transform stack (rlgl-style), applied CPU-side to the vertices that
    // follow. Composes in local space: Translate then Rotate draws rotated
    // around the translated origin.
    void PushMatrix();   // saves the current transform
    void PopMatrix();    // restores the last saved one
    void LoadIdentity(); // resets the current transform
    void Translate(f32 x, f32 y, f32 z = 0.f);
    void Rotate(f32 angleDeg, f32 ax, f32 ay, f32 az); // axis-angle
    void Scale(f32 x, f32 y, f32 z = 1.f);
    void SetTransform(const f32* mat4); // sets it directly (nullptr = identity)

    // current state for the vertices that follow
    void SetMode(RenderPrimitive prim);  // LINES or TRIANGLES
    void SetTexture(const Texture* tex); // nullptr = white 1x1
    void SetColor(u8 r, u8 g, u8 b, u8 a = 255);
    void SetColorF(f32 r, f32 g, f32 b, f32 a = 1.0f);
    void TexCoord(f32 u, f32 v);

    // emit a vertex with the current color/uv
    void Vertex2(f32 x, f32 y);
    void Vertex3(f32 x, f32 y, f32 z);

    // 2D shapes (LINES for outlines, TRIANGLES for fill)
    void Line(f32 x0, f32 y0, f32 x1, f32 y1);
    void Rect(f32 x, f32 y, f32 w, f32 h, bool fill = true);
    void Circle(f32 cx, f32 cy, f32 radius, bool fill = true, int segments = 32);
    void Triangle(f32 x1, f32 y1, f32 x2, f32 y2, f32 x3, f32 y3, bool fill = true);
    void Ellipse(f32 cx, f32 cy, f32 rx, f32 ry, bool fill = true, int segments = 32);
    void Ring(f32 cx, f32 cy, f32 rInner, f32 rOuter, bool fill = true, int segments = 32);
    void Arc(f32 cx, f32 cy, f32 radius, f32 startDeg, f32 endDeg, int segments = 16);
    void ThickLine(f32 x0, f32 y0, f32 x1, f32 y1, f32 thickness);
    void Polygon(f32 cx, f32 cy, int sides, f32 radius, f32 rotationDeg, bool fill = true);
    void Polyline(const f32* xyPairs, int pointCount); // {x0,y0, x1,y1, ...}
    void Grid(f32 x, f32 y, f32 w, f32 h, f32 cellW, f32 cellH);

    // 3D wireframe debug shapes (LINES; use the transform stack to orient them)
    void CubeWire(f32 cx, f32 cy, f32 cz, f32 sx, f32 sy, f32 sz);
    void SphereWire(f32 cx, f32 cy, f32 cz, f32 radius, int rings = 6, int slices = 16);
    void CylinderWire(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int slices = 16);
    void CapsuleWire(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int slices = 16);
    void Grid3D(f32 size, f32 step); // XZ plane at y=0
    void Axes(f32 size);             // X=red, Y=green, Z=blue

    // 3D solid shapes (TRIANGLES), flat current color
    void Cube(f32 cx, f32 cy, f32 cz, f32 sx, f32 sy, f32 sz);
    void Sphere(f32 cx, f32 cy, f32 cz, f32 radius, int rings = 12, int slices = 24);
    void Cylinder(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int slices = 24);
    void Capsule(f32 cx, f32 cy, f32 cz, f32 radius, f32 height, int rings = 6, int slices = 24);

    // textured quad; the sub-rect overload takes pixel coords into the texture
    void Quad(const Texture& tex, f32 x, f32 y, f32 w, f32 h);
    void Quad(const Texture& tex, f32 srcX, f32 srcY, f32 srcW, f32 srcH, f32 x, f32 y, f32 w,
              f32 h);

    // text with the embedded 8x8 font (ASCII 32..127, '\n' supported);
    // size = glyph height in world/pixel units, uses the current color
    void Text(f32 x, f32 y, f32 size, const char* text);
    f32 TextWidth(f32 size, const char* text) const;

    // flush everything accumulated so far
    void Render();

    u32 GetVertexCount() const { return used; }
    u32 GetIndexCount() const { return iused; }

private:
    struct Vertex
    {
        f32 x, y, z;
        f32 u, v;
        u32 rgba;
    };

    struct DrawCmd
    {
        u32 texture;
        RenderPrimitive mode;
        u32 start;
        u32 count;
    };

    // reserves vertices + indices (auto-flush); idx values are relative,
    // write them as base + k
    bool reserve(u32 vcount, u32 icount, Vertex*& v, u16*& idx, u32& base);
    void closeCmd();
    // multiplies count vertices in place by the current transform (no-op if off)
    void applyTransform(Vertex* v, u32 count);

    Vertex* verts = nullptr;
    u32 capacity = 0;
    u32 used = 0;

    u16* indices = nullptr; // u16: the batch never exceeds 65535 verts per flush
    u32 icapacity = 0;
    u32 iused = 0;

    DrawCmd* cmds = nullptr;
    u32 cmdCapacity = 0;
    u32 cmdCount = 0;
    u32 cmdStart = 0; // first index of the open command

    u32 curTexture = 0;
    RenderPrimitive curMode = RenderPrimitive::TRIANGLES;
    u32 curColor = 0xFFFFFFFFu;
    f32 curU = 0.f, curV = 0.f;
    f32 proj[16];

    // transform stack; xform is the live top, stack[] holds the saved levels
    static const int kStackDepth = 32;
    f32 xform[16];
    f32 stack[kStackDepth][16];
    bool stackActive[kStackDepth];
    int stackTop = 0;
    bool hasTransform = false;

    void multiply(const f32* m); // xform = xform * m

    Shader shader;
    Buffer vbo;
    Buffer ibo;
    VertexArray vao;
    Texture white;
    Texture font; // embedded 8x8 font atlas (built in Init)
    i32 mvpLoc = -1;
    bool ready = false;
};

} // namespace gl
