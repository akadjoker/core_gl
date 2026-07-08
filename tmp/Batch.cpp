#include "pch.h"
#include "Batch.hpp"
#include "Opengl.hpp"
#include "Texture.hpp"
#include "Shader.hpp"
#include "Color.hpp"
#include "RenderState.hpp"

// #include <algorithm>
// #include <cmath>
// #include <cstddef>
// #include <cstdio>

#define LINES     0x0001
#define TRIANGLES 0x0004
#define QUAD      0x0008
#define PI        3.14159265358979323846f
#define DEG2RAD   (PI / 180.0f)

// ---------------------------------------------------------------------------
// Anonymous helpers
// ---------------------------------------------------------------------------
namespace
{
    static unsigned char floatToU8(float v)
    {
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        return (unsigned char)(v * 255.0f);
    }

    static GLenum toGLMode(int mode)
    {
        return (mode == LINES) ? GL_LINES : GL_TRIANGLES;
    }

    static GLuint compileShader(GLenum type, const char *src)
    {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[2048]; GLsizei written = 0;
            glGetShaderInfoLog(s, (GLsizei)sizeof(log), &written, log);
            SDL_Log("[Batch] shader compile error: %.*s", (int)written, log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    }
} // namespace

// ---------------------------------------------------------------------------
// Ctor / Dtor / Init / Release
// ---------------------------------------------------------------------------

RenderBatch::RenderBatch()
    : maxVertices(0), currentMode(QUAD), currentTexture(0), currentDepth(0.0f),
      gpuReady(false), use_matrix(false),
      modelMatrix(Mat4::Identity()), viewMatrix(Mat4::Identity()),
      texcoordx(0.0f), texcoordy(0.0f), colorr(255), colorg(255), colorb(255), colora(255),
      programId(0), vaoId(0), vboId(0), eboId(0), whiteTextureId(0),
      uMvpLocation(-1), uTextureLocation(-1),
      aPosLocation(-1), aUvLocation(-1), aColorLocation(-1)
{
}

RenderBatch::~RenderBatch()
{
    Release();
}

void RenderBatch::Init(int /*numBuffers*/, int bufferElements)
{
    maxVertices = std::max(bufferElements * 4, 256);
    vertices.clear();
    draws.clear();
    vertices.reserve((std::size_t)maxVertices);
    draws.reserve(1024);
    currentMode    = QUAD;
    currentTexture = 0;
    currentDepth   = 0.0f;
    texcoordx = 0.0f; texcoordy = 0.0f;
    colorr = 255; colorg = 255; colorb = 255; colora = 255;
    use_matrix  = false;
    modelMatrix = Mat4::Identity();
    viewMatrix  = Mat4::Identity();
    createDeviceObjects();
}

void RenderBatch::Release()
{
    destroyDeviceObjects();
    vertices.clear();
    draws.clear();
    quadIndices.clear();
    maxVertices    = 0;
    currentTexture = 0;
    currentMode    = QUAD;
    use_matrix     = false;
}

// ---------------------------------------------------------------------------
// GPU object management
// ---------------------------------------------------------------------------

bool RenderBatch::compileShaderProgram()
{
    static const char *kVS =
        "#version 130\n"
        "in vec3 aPos;\n"
        "in vec2 aUV;\n"
        "in vec4 aColor;\n"
        "uniform mat4 uMVP;\n"
        "out vec2 vUV;\n"
        "out vec4 vColor;\n"
        "void main() {\n"
        "    vUV = aUV;\n"
        "    vColor = aColor;\n"
        "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
        "}\n";

    static const char *kFS =
        "#version 130\n"
        "uniform sampler2D uTexture;\n"
        "in vec2 vUV;\n"
        "in vec4 vColor;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = texture(uTexture, vUV) * vColor;\n"
        "}\n";

    GLuint vs = compileShader(GL_VERTEX_SHADER,   kVS);
    if (!vs) return false;
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFS);
    if (!fs) { glDeleteShader(vs); return false; }

    programId = glCreateProgram();
    glAttachShader(programId, vs);
    glAttachShader(programId, fs);
    glBindAttribLocation(programId, 0, "aPos");
    glBindAttribLocation(programId, 1, "aUV");
    glBindAttribLocation(programId, 2, "aColor");
    glLinkProgram(programId);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(programId, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[2048]; GLsizei written = 0;
        glGetProgramInfoLog(programId, (GLsizei)sizeof(log), &written, log);
        SDL_Log("[Batch] program link error: %.*s", (int)written, log);
        glDeleteProgram(programId); programId = 0;
        return false;
    }

    aPosLocation      = 0;
    aUvLocation       = 1;
    aColorLocation    = 2;
    uMvpLocation      = glGetUniformLocation(programId, "uMVP");
    uTextureLocation  = glGetUniformLocation(programId, "uTexture");
    return true;
}

bool RenderBatch::createDeviceObjects()
{
    destroyDeviceObjects();
    if (!compileShaderProgram()) return false;

    glGenVertexArrays(1, &vaoId);
    glGenBuffers(1, &vboId);
    glGenBuffers(1, &eboId);
    if (!vaoId || !vboId || !eboId)
    {
        SDL_Log("[Batch] failed to create VAO/VBO/EBO");
        destroyDeviceObjects(); return false;
    }

    glBindVertexArray(vaoId);
    glBindBuffer(GL_ARRAY_BUFFER, vboId);
    glEnableVertexAttribArray((GLuint)aPosLocation);
    glEnableVertexAttribArray((GLuint)aUvLocation);
    glEnableVertexAttribArray((GLuint)aColorLocation);
    glVertexAttribPointer((GLuint)aPosLocation,   3, GL_FLOAT,         GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, x));
    glVertexAttribPointer((GLuint)aUvLocation,    2, GL_FLOAT,         GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, u));
    glVertexAttribPointer((GLuint)aColorLocation, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(Vertex), (const void*)offsetof(Vertex, r));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eboId);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // 1x1 white texture
    const unsigned char white[4] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTextureId);
    glBindTexture(GL_TEXTURE_2D, whiteTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, 0);

    gpuReady = ensureIndexCapacity((std::size_t)maxVertices);
    if (currentTexture == 0) currentTexture = whiteTextureId;
    return gpuReady;
}

void RenderBatch::destroyDeviceObjects()
{
    if (whiteTextureId) { glDeleteTextures(1, &whiteTextureId); whiteTextureId = 0; }
    if (eboId)          { glDeleteBuffers(1, &eboId);           eboId = 0; }
    if (vboId)          { glDeleteBuffers(1, &vboId);           vboId = 0; }
    if (vaoId)          { glBindVertexArray(0); glDeleteVertexArrays(1, &vaoId); vaoId = 0; }
    if (programId)      { glDeleteProgram(programId);           programId = 0; }
    uMvpLocation = uTextureLocation = aPosLocation = aUvLocation = aColorLocation = -1;
    gpuReady = false;
}

bool RenderBatch::ensureIndexCapacity(std::size_t vertexCount)
{
    if (!eboId) return false;
    const std::size_t quadCount  = (vertexCount + 3) / 4;
    const std::size_t indexCount = quadCount * 6;
    if (quadIndices.size() >= indexCount) return true;

    quadIndices.resize(indexCount);
    for (std::size_t i = 0; i < quadCount; ++i)
    {
        const unsigned int b  = (unsigned int)(i * 4);
        const std::size_t  bi = i * 6;
        quadIndices[bi+0] = b+0; quadIndices[bi+1] = b+1; quadIndices[bi+2] = b+2;
        quadIndices[bi+3] = b+0; quadIndices[bi+4] = b+2; quadIndices[bi+5] = b+3;
    }

    glBindVertexArray(vaoId);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eboId);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(quadIndices.size() * sizeof(unsigned int)),
                 quadIndices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    return true;
}

// ---------------------------------------------------------------------------
// Core batch primitives
// ---------------------------------------------------------------------------

bool RenderBatch::CheckRenderBatchLimit(int vCount)
{
    if (maxVertices > 0 && (int)vertices.size() + vCount > maxVertices)
    {
        int      savedMode = currentMode;
        unsigned savedTex  = currentTexture;
        Render();
        currentMode    = savedMode;
        currentTexture = savedTex;
        return true;
    }
    return false;
}

void RenderBatch::ensureDrawCall()
{
    if (!draws.empty())
    {
        DrawCall &last = draws.back();
        if (last.mode == currentMode && last.textureId == currentTexture)
            return;
    }

    if (currentMode == QUAD && !vertices.empty() && vertices.size() % 4 != 0)
    {
        int      savedMode = currentMode;
        unsigned savedTex  = currentTexture;
        Render();
        currentMode    = savedMode;
        currentTexture = savedTex;
    }

    DrawCall call;
    call.mode      = currentMode;
    call.textureId = currentTexture;
    call.first     = (int)vertices.size();
    call.count     = 0;
    draws.push_back(call);
}

void RenderBatch::SetMode(int mode)
{
    currentMode = mode;
}

void RenderBatch::SetTexture(unsigned int id)
{
    currentTexture = (id != 0) ? id : whiteTextureId;
}

void RenderBatch::SetTexture(Texture2D *texture)
{
    SetTexture(texture ? texture->GetID() : 0u);
}

Vec3 RenderBatch::transformPoint(const Vec3 &p) const
{
    if (!use_matrix) return p;
    const Vec4 t = modelMatrix * Vec4(p, 1.0f);
    return Vec3(t.x, t.y, t.z);
}

void RenderBatch::Vertex3f(float x, float y, float z)
{
    CheckRenderBatchLimit(1);
    ensureDrawCall();

    const Vec3 p = transformPoint({x, y, z});

    Vertex v;
    v.x = p.x; v.y = p.y; v.z = p.z;
    v.u = texcoordx; v.v = texcoordy;
    v.r = colorr; v.g = colorg; v.b = colorb; v.a = colora;
    vertices.push_back(v);
    draws.back().count++;
}

void RenderBatch::Vertex2f(float x, float y)
{
    Vertex3f(x, y, currentDepth);
}

void RenderBatch::TexCoord2f(float x, float y)
{
    texcoordx = x; texcoordy = y;
}

void RenderBatch::SetColor(const Color &c)
{
    colorr = c.r; colorg = c.g; colorb = c.b; colora = c.a;
}

void RenderBatch::SetColor(float r, float g, float b)
{
    colorr = floatToU8(r); colorg = floatToU8(g); colorb = floatToU8(b);
}

void RenderBatch::SetColor(u8 r, u8 g, u8 b, u8 a)
{
    colorr = r; colorg = g; colorb = b; colora = a;
}

void RenderBatch::SetAlpha(float a) { colora = floatToU8(a); }

void RenderBatch::BeginTransform(const Mat4&transform)
{
    modelMatrix = transform;
    use_matrix  = true;
}

void RenderBatch::EndTransform()
{
    use_matrix = false;
}

void RenderBatch::SetMatrix(const Mat4&matrix)
{
    viewMatrix = matrix;
}

// ---------------------------------------------------------------------------
// Render / flush
// ---------------------------------------------------------------------------

void RenderBatch::Render()
{
    if (vertices.empty() || draws.empty()) return;
    if (!gpuReady && !createDeviceObjects()) return;
    if (!ensureIndexCapacity(vertices.size())) return;

    RenderState::Instance().UseProgram(programId);
    if (uMvpLocation >= 0)
        glUniformMatrix4fv(uMvpLocation, 1, GL_FALSE, &viewMatrix.c[0][0]);
    if (uTextureLocation >= 0)
        glUniform1i(uTextureLocation, 0);

    glBindVertexArray(vaoId);
    glBindBuffer(GL_ARRAY_BUFFER, vboId);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_DYNAMIC_DRAW);

    //glActiveTexture(GL_TEXTURE0);
    unsigned int activeTex = 0xFFFFFFFFu;

    for (std::size_t i = 0; i < draws.size(); ++i)
    {
        const DrawCall &draw = draws[i];
        if (draw.count == 0) continue;

        const unsigned int tex = (draw.textureId != 0) ? draw.textureId : whiteTextureId;
        if (tex != activeTex)
        {
//            glBindTexture(GL_TEXTURE_2D, tex);
            RenderState::Instance().BindTexture(0, GL_TEXTURE_2D,   tex);
            activeTex = tex;
        }

        if (draw.mode == QUAD)
        {
            const GLint   firstIndex = (GLint)(draw.first / 4) * 6;
            const GLsizei count      = (GLsizei)(draw.count / 4) * 6;
            if (count > 0)
                glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT,
                               (const void*)(firstIndex * sizeof(unsigned int)));
        }
        else
        {
            glDrawArrays(toGLMode(draw.mode), (GLint)draw.first, (GLsizei)draw.count);
        }
    }

    glBindVertexArray(0);
    //glBindTexture(GL_TEXTURE_2D, 0);
    RenderState::Instance().UseProgram(0);
    RenderState::Instance().BindTexture(0, GL_TEXTURE_2D,   0);

    vertices.clear();
    draws.clear();
}

// ===========================================================================
// 2D Shapes
// ===========================================================================

void RenderBatch::Line2D(int x0, int y0, int x1, int y1)
{
    SetTexture(0u); SetMode(LINES);
    Vertex2f((float)x0, (float)y0);
    Vertex2f((float)x1, (float)y1);
}

void RenderBatch::Line2D(const Vec2 &start, const Vec2 &end)
{
    SetTexture(0u); SetMode(LINES);
    Vertex2f(start.x, start.y);
    Vertex2f(end.x, end.y);
}

void RenderBatch::Circle(int centerX, int centerY, float radius, bool fill)
{
    if (radius <= 0.0f) return;
    SetTexture(0u);
    const int   segments = std::max(18, std::min(96, (int)(radius * 0.8f)));
    const float step     = (2.0f * PI) / (float)segments;
    const float cx = (float)centerX, cy = (float)centerY;

    if (fill)
    {
        SetMode(TRIANGLES);
        for (int i = 0; i < segments; ++i)
        {
            const float a0 = step * i, a1 = step * (i + 1);
            Vertex2f(cx, cy);
            Vertex2f(cx + cosf(a0)*radius, cy + sinf(a0)*radius);
            Vertex2f(cx + cosf(a1)*radius, cy + sinf(a1)*radius);
        }
    }
    else
    {
        SetMode(LINES);
        for (int i = 0; i < segments; ++i)
        {
            const float a0 = step * i, a1 = step * (i + 1);
            Vertex2f(cx + cosf(a0)*radius, cy + sinf(a0)*radius);
            Vertex2f(cx + cosf(a1)*radius, cy + sinf(a1)*radius);
        }
    }
}

void RenderBatch::Rectangle(int posX, int posY, int width, int height, bool fill)
{
    SetTexture(0u);
    if (fill)
    {
        SetMode(QUAD);
        Vertex2f((float)posX,         (float)posY);
        Vertex2f((float)(posX+width), (float)posY);
        Vertex2f((float)(posX+width), (float)(posY+height));
        Vertex2f((float)posX,         (float)(posY+height));
    }
    else
    {
        SetMode(LINES);
        Vertex2f((float)posX,         (float)posY);
        Vertex2f((float)(posX+width), (float)posY);
        Vertex2f((float)(posX+width), (float)posY);
        Vertex2f((float)(posX+width), (float)(posY+height));
        Vertex2f((float)(posX+width), (float)(posY+height));
        Vertex2f((float)posX,         (float)(posY+height));
        Vertex2f((float)posX,         (float)(posY+height));
        Vertex2f((float)posX,         (float)posY);
    }
}

void RenderBatch::Triangle(float x1, float y1, float x2, float y2, float x3, float y3, bool fill)
{
    SetTexture(0u);
    if (fill)
    {
        SetMode(TRIANGLES);
        Vertex2f(x1, y1); Vertex2f(x2, y2); Vertex2f(x3, y3);
    }
    else
    {
        SetMode(LINES);
        Vertex2f(x1, y1); Vertex2f(x2, y2);
        Vertex2f(x2, y2); Vertex2f(x3, y3);
        Vertex2f(x3, y3); Vertex2f(x1, y1);
    }
}

void RenderBatch::Ellipse(int centerX, int centerY, float radiusX, float radiusY, bool fill)
{
    SetTexture(0u);
    const int   segments = 64;
    const float angleInc = 2.0f * PI / (float)segments;
    const float cx = (float)centerX, cy = (float)centerY;

    if (fill)
    {
        SetMode(TRIANGLES);
        float angle = 0.0f;
        for (int i = 0; i < segments; ++i)
        {
            Vertex2f(cx, cy);
            Vertex2f(cx + cosf(angle)*radiusX, cy + sinf(angle)*radiusY);
            angle += angleInc;
            Vertex2f(cx + cosf(angle)*radiusX, cy + sinf(angle)*radiusY);
        }
    }
    else
    {
        SetMode(LINES);
        float angle = 0.0f;
        for (int i = 0; i < segments; ++i)
        {
            Vertex2f(cx + cosf(angle)*radiusX, cy + sinf(angle)*radiusY);
            angle += angleInc;
            Vertex2f(cx + cosf(angle)*radiusX, cy + sinf(angle)*radiusY);
        }
    }
}

void RenderBatch::Polygon(int centerX, int centerY, int sides, float radius, float rotation, bool fill)
{
    if (sides < 3) return;
    SetTexture(0u);
    const float angleInc = 2.0f * PI / (float)sides;
    float angle = rotation * DEG2RAD;

    if (fill)
    {
        SetMode(TRIANGLES);
        for (int i = 0; i < sides; ++i)
        {
            Vertex2f((float)centerX, (float)centerY);
            Vertex2f(centerX + cosf(angle)*radius, centerY + sinf(angle)*radius);
            angle += angleInc;
            Vertex2f(centerX + cosf(angle)*radius, centerY + sinf(angle)*radius);
        }
    }
    else
    {
        SetMode(LINES);
        for (int i = 0; i < sides; ++i)
        {
            Vertex2f(centerX + cosf(angle)*radius, centerY + sinf(angle)*radius);
            angle += angleInc;
            Vertex2f(centerX + cosf(angle)*radius, centerY + sinf(angle)*radius);
        }
    }
}

void RenderBatch::Polyline(const Vec2 *points, int pointCount)
{
    if (!points || pointCount < 2) return;
    SetTexture(0u); SetMode(LINES);
    for (int i = 0; i < pointCount - 1; ++i)
    {
        Vertex2f(points[i].x, points[i].y);
        Vertex2f(points[i+1].x, points[i+1].y);
    }
}

void RenderBatch::RoundedRectangle(int posX, int posY, int width, int height,
                                   float roundness, int segments, bool fill)
{
    if (width <= 0 || height <= 0) return;
    if (roundness <= 0.0f) { Rectangle(posX, posY, width, height, fill); return; }
    SetTexture(0u);

    float radius = roundness;
    radius = std::min(radius, width  * 0.5f);
    radius = std::min(radius, height * 0.5f);
    segments = std::max(2, std::min(segments, 32));

    const float cxTL = posX + radius,          cyTL = posY + radius;
    const float cxTR = posX + width - radius,  cyTR = posY + radius;
    const float cxBR = posX + width - radius,  cyBR = posY + height - radius;
    const float cxBL = posX + radius,          cyBL = posY + height - radius;

    const int totalPoints = segments * 4;
    std::vector<Vec2> pts((size_t)totalPoints);

    auto fillCorner = [&](int base, float cx, float cy, float startDeg, float endDeg)
    {
        const float startRad = startDeg * DEG2RAD;
        const float step     = (endDeg - startDeg) * DEG2RAD / (float)segments;
        for (int i = 0; i < segments; ++i)
        {
            const float a = startRad + step * (float)i;
            pts[(size_t)(base + i)] = { cx + cosf(a)*radius, cy + sinf(a)*radius };
        }
    };

    fillCorner(0*segments, cxTL, cyTL, 180.0f, 270.0f);
    fillCorner(1*segments, cxTR, cyTR, 270.0f, 360.0f);
    fillCorner(2*segments, cxBR, cyBR,   0.0f,  90.0f);
    fillCorner(3*segments, cxBL, cyBL,  90.0f, 180.0f);

    if (fill)
    {
        SetMode(TRIANGLES);
        const float cx = posX + width  * 0.5f;
        const float cy = posY + height * 0.5f;
        for (int i = 0; i < totalPoints; ++i)
        {
            const Vec2 &a = pts[(size_t)i];
            const Vec2 &b = pts[(size_t)((i + 1) % totalPoints)];
            Vertex2f(cx, cy); Vertex2f(b.x, b.y); Vertex2f(a.x, a.y);
        }
    }
    else
    {
        SetMode(LINES);
        for (int i = 0; i < totalPoints; ++i)
        {
            const Vec2 &a = pts[(size_t)i];
            const Vec2 &b = pts[(size_t)((i + 1) % totalPoints)];
            Vertex2f(a.x, a.y); Vertex2f(b.x, b.y);
        }
    }
}

void RenderBatch::CircleSector(int centerX, int centerY, float radius,
                               float startAngle, float endAngle, int segments, bool fill)
{
    segments = std::max(1, segments);
    SetTexture(0u);
    const float angleStep = (endAngle - startAngle) / (float)segments;
    float angle = startAngle * DEG2RAD;

    if (fill)
    {
        SetMode(TRIANGLES);
        for (int i = 0; i < segments; ++i)
        {
            Vertex2f((float)centerX, (float)centerY);
            Vertex2f(centerX + cosf(angle)*radius, centerY + sinf(angle)*radius);
            angle += angleStep * DEG2RAD;
            Vertex2f(centerX + cosf(angle)*radius, centerY + sinf(angle)*radius);
        }
    }
    else
    {
        SetMode(LINES);
        for (int i = 0; i < segments; ++i)
        {
            Vertex2f(centerX + cosf(angle)*radius, centerY + sinf(angle)*radius);
            angle += angleStep * DEG2RAD;
            Vertex2f(centerX + cosf(angle)*radius, centerY + sinf(angle)*radius);
        }
        const float startRad = startAngle * DEG2RAD;
        const float endRad   = endAngle   * DEG2RAD;
        Vertex2f((float)centerX, (float)centerY);
        Vertex2f(centerX + cosf(startRad)*radius, centerY + sinf(startRad)*radius);
        Vertex2f((float)centerX, (float)centerY);
        Vertex2f(centerX + cosf(endRad)*radius,   centerY + sinf(endRad)*radius);
    }
}

void RenderBatch::Grid(int posX, int posY, int width, int height, int cellW, int cellH)
{
    if (cellW <= 0 || cellH <= 0) return;
    SetTexture(0u); SetMode(LINES);
    for (int x = posX; x <= posX + width; x += cellW)
    {
        Vertex2f((float)x, (float)posY);
        Vertex2f((float)x, (float)(posY + height));
    }
    for (int y = posY; y <= posY + height; y += cellH)
    {
        Vertex2f((float)posX, (float)y);
        Vertex2f((float)(posX + width), (float)y);
    }
}

void RenderBatch::ThickLine2D(float x1, float y1, float x2, float y2, float thickness)
{
    float dx = x2 - x1, dy = y2 - y1;
    const float len = sqrtf(dx*dx + dy*dy);
    if (len <= 0.0001f) return;
    dx /= len; dy /= len;
    const float hx = -dy * thickness * 0.5f;
    const float hy =  dx * thickness * 0.5f;
    SetTexture(0u); SetMode(TRIANGLES);
    Vertex2f(x1+hx, y1+hy); Vertex2f(x1-hx, y1-hy); Vertex2f(x2+hx, y2+hy);
    Vertex2f(x1-hx, y1-hy); Vertex2f(x2-hx, y2-hy); Vertex2f(x2+hx, y2+hy);
}

void RenderBatch::Ring(int centerX, int centerY, float innerRadius, float outerRadius,
                       float startAngle, float endAngle, int segments, bool fill)
{
    if (innerRadius < 0.0f) innerRadius = 0.0f;
    if (outerRadius <= innerRadius) return;
    segments = std::max(4, segments);
    SetTexture(0u);

    const float startRad = startAngle * DEG2RAD;
    const float endRad   = endAngle   * DEG2RAD;
    const float step     = (endRad - startRad) / (float)segments;
    const float cx = (float)centerX, cy = (float)centerY;

    if (fill)
    {
        SetMode(TRIANGLES);
        float angle = startRad;
        for (int i = 0; i < segments; ++i)
        {
            const float a1 = angle, a2 = angle + step;
            const float ox0 = cx + cosf(a1)*outerRadius, oy0 = cy + sinf(a1)*outerRadius;
            const float ox1 = cx + cosf(a2)*outerRadius, oy1 = cy + sinf(a2)*outerRadius;
            const float ix0 = cx + cosf(a1)*innerRadius, iy0 = cy + sinf(a1)*innerRadius;
            const float ix1 = cx + cosf(a2)*innerRadius, iy1 = cy + sinf(a2)*innerRadius;
            Vertex2f(ix0,iy0); Vertex2f(ox0,oy0); Vertex2f(ox1,oy1);
            Vertex2f(ix0,iy0); Vertex2f(ox1,oy1); Vertex2f(ix1,iy1);
            angle = a2;
        }
    }
    else
    {
        SetMode(LINES);
        float angle = startRad;
        for (int i = 0; i < segments; ++i)
        {
            const float a1 = angle, a2 = angle + step;
            Vertex2f(cx + cosf(a1)*outerRadius, cy + sinf(a1)*outerRadius);
            Vertex2f(cx + cosf(a2)*outerRadius, cy + sinf(a2)*outerRadius);
            Vertex2f(cx + cosf(a1)*innerRadius, cy + sinf(a1)*innerRadius);
            Vertex2f(cx + cosf(a2)*innerRadius, cy + sinf(a2)*innerRadius);
            angle = a2;
        }
        Vertex2f(cx + cosf(startRad)*innerRadius, cy + sinf(startRad)*innerRadius);
        Vertex2f(cx + cosf(startRad)*outerRadius, cy + sinf(startRad)*outerRadius);
        Vertex2f(cx + cosf(endRad)*innerRadius,   cy + sinf(endRad)*innerRadius);
        Vertex2f(cx + cosf(endRad)*outerRadius,   cy + sinf(endRad)*outerRadius);
    }
}

void RenderBatch::Arc(int centerX, int centerY, float radius,
                      float startAngle, float endAngle, float thickness, int segments)
{
    if (thickness <= 0.0f || radius <= 0.0f) return;
    const float inner = std::max(0.0f, radius - thickness * 0.5f);
    const float outer = radius + thickness * 0.5f;
    Ring(centerX, centerY, inner, outer, startAngle, endAngle, segments, true);
}

// ===========================================================================
// Quads / sprites
// ===========================================================================

void RenderBatch::Quad(const Vec2 *coords, const Vec2 *texcoords)
{
    SetMode(QUAD);
    for (int i = 0; i < 4; ++i)
    {
        TexCoord2f(texcoords[i].x, texcoords[i].y);
        Vertex2f(coords[i].x, coords[i].y);
    }
}

void RenderBatch::Quad(Texture *texture, const Vec2 *coords, const Vec2 *texcoords)
{
    SetTexture(texture ? texture->GetID() : 0u);
    Quad(coords, texcoords);
}

void RenderBatch::Quad(Texture2D *texture, const Vec2 *coords, const Vec2 *texcoords)
{
    Quad(static_cast<Texture *>(texture), coords, texcoords);
}

void RenderBatch::Quad(u32 texture, float x, float y, float width, float height)
{
    SetTexture(texture); SetMode(QUAD);
    TexCoord2f(0.0f, 0.0f); Vertex2f(x, y);
    TexCoord2f(1.0f, 0.0f); Vertex2f(x + width, y);
    TexCoord2f(1.0f, 1.0f); Vertex2f(x + width, y + height);
    TexCoord2f(0.0f, 1.0f); Vertex2f(x, y + height);
}

void RenderBatch::Quad(Texture2D *texture, float x, float y, float width, float height)
{
    Quad(static_cast<Texture *>(texture), x, y, width, height);
}

void RenderBatch::Quad(Texture *texture, float x, float y, float width, float height)
{
    SetTexture(texture ? texture->GetID() : 0u); SetMode(QUAD);
    TexCoord2f(0.0f, 0.0f); Vertex2f(x, y);
    TexCoord2f(1.0f, 0.0f); Vertex2f(x + width, y);
    TexCoord2f(1.0f, 1.0f); Vertex2f(x + width, y + height);
    TexCoord2f(0.0f, 1.0f); Vertex2f(x, y + height);
}

static void calcSrcUV(const FloatRect &src, float tw, float th,
                      float &l, float &r, float &t, float &b)
{
    const float sw = (tw > 0.0f) ? tw : 1.0f;
    const float sh = (th > 0.0f) ? th : 1.0f;
    l = (2.0f * src.x + 1.0f) / (2.0f * sw);
    r = l + (src.width  * 2.0f - 2.0f) / (2.0f * sw);
    t = (2.0f * src.y + 1.0f) / (2.0f * sh);
    b = t + (src.height * 2.0f - 2.0f) / (2.0f * sh);
}

void RenderBatch::Quad(Texture2D *texture, const FloatRect &src, float x, float y,
                       float width, float height)
{
    Quad(static_cast<Texture *>(texture), src, x, y, width, height);
}

void RenderBatch::Quad(Texture *texture, const FloatRect &src, float x, float y,
                       float width, float height)
{
    float l, r, t, b;
    const float tw = texture ? (float)texture->GetWidth()  : 1.0f;
    const float th = texture ? (float)texture->GetHeight() : 1.0f;
    calcSrcUV(src, tw, th, l, r, t, b);
    SetTexture(texture ? texture->GetID() : 0u); SetMode(QUAD);
    TexCoord2f(l, t); Vertex2f(x,         y);
    TexCoord2f(r, t); Vertex2f(x + width, y);
    TexCoord2f(r, b); Vertex2f(x + width, y + height);
    TexCoord2f(l, b); Vertex2f(x,         y + height);
}

void RenderBatch::Quad(Texture2D *texture, float x1, float y1, float x2, float y2,
                       const FloatRect &src)
{
    Quad(texture, src, x1, y1, x2 - x1, y2 - y1);
}

void RenderBatch::Quad(Texture *texture, float x1, float y1, float x2, float y2,
                       const FloatRect &src)
{
    Quad(texture, src, x1, y1, x2 - x1, y2 - y1);
}

void RenderBatch::QuadCentered(Texture2D *texture, float x, float y, float size, const FloatRect &clip)
{
    const float tw = texture ? (float)texture->GetWidth()  : 1.0f;
    const float th = texture ? (float)texture->GetHeight() : 1.0f;
    float l, r, t, b;
    calcSrcUV(clip, tw, th, l, r, t, b);
    SetTexture(texture ? texture->GetID() : 0u);
    const float qs = size * 80.0f;
    Vec2 coords[4]    = {{x-qs,y-qs},{x-qs,y+qs},{x+qs,y+qs},{x+qs,y-qs}};
    Vec2 texcoords[4] = {{l,t},{l,b},{r,b},{r,t}};
    Quad(coords, texcoords);
}

void RenderBatch::DrawQuad(float x1, float y1, float x2, float y2,
                           float u0, float v0, float u1, float v1)
{
    Vec2 coords[4]    = {{x1,y1},{x1,y2},{x2,y2},{x2,y1}};
    Vec2 texcoords[4] = {{u0,v0},{u0,v1},{u1,v1},{u1,v0}};
    Quad(coords, texcoords);
}

void RenderBatch::DrawQuad(Texture2D *texture, float x1, float y1, float x2, float y2,
                           float u0, float v0, float u1, float v1)
{
    SetTexture(texture);
    DrawQuad(x1, y1, x2, y2, u0, v0, u1, v1);
}

void RenderBatch::DrawQuad(float x1, float y1, float x2, float y2,
                           float u0, float v0, float u1, float v1, const Color &color)
{
    SetColor(color);
    DrawQuad(x1, y1, x2, y2, u0, v0, u1, v1);
}

void RenderBatch::TexturedRect(unsigned int textureId, float x, float y, float w, float h)
{
    Quad(textureId, x, y, w, h);
}

void RenderBatch::Sprite(unsigned int textureId,
                         float srcX, float srcY, float srcW, float srcH,
                         float dstX, float dstY, float dstW, float dstH,
                         float texWidth, float texHeight)
{
    const float sw = (texWidth  > 0.0f) ? texWidth  : 1.0f;
    const float sh = (texHeight > 0.0f) ? texHeight : 1.0f;
    const float u0 = srcX / sw, v0 = srcY / sh;
    const float u1 = (srcX + srcW) / sw, v1 = (srcY + srcH) / sh;
    SetTexture(textureId); SetMode(QUAD);
    TexCoord2f(u0, v0); Vertex2f(dstX,        dstY);
    TexCoord2f(u1, v0); Vertex2f(dstX + dstW, dstY);
    TexCoord2f(u1, v1); Vertex2f(dstX + dstW, dstY + dstH);
    TexCoord2f(u0, v1); Vertex2f(dstX,        dstY + dstH);
}

void RenderBatch::SpriteEx(unsigned int textureId,
                           float srcX, float srcY, float srcW, float srcH,
                           float dstX, float dstY, float dstW, float dstH,
                           float angle, float originX, float originY,
                           bool flipH, bool flipV,
                           float texWidth, float texHeight)
{
    const float sw = (texWidth  > 0.0f) ? texWidth  : 1.0f;
    const float sh = (texHeight > 0.0f) ? texHeight : 1.0f;
    float u0 = srcX / sw, v0 = srcY / sh;
    float u1 = (srcX + srcW) / sw, v1 = (srcY + srcH) / sh;
    if (flipH) { float tmp = u0; u0 = u1; u1 = tmp; }
    if (flipV) { float tmp = v0; v0 = v1; v1 = tmp; }

    const float lx = -originX,    rx = dstW - originX;
    const float ty = -originY,    by = dstH - originY;
    const float rad = angle * DEG2RAD;
    const float ca = cosf(rad), sa = sinf(rad);
    const float cx = dstX + originX, cy = dstY + originY;

    SetTexture(textureId); SetMode(TRIANGLES);
    const float x0 = cx + lx*ca - ty*sa, y0 = cy + lx*sa + ty*ca;
    const float x1 = cx + rx*ca - ty*sa, y1 = cy + rx*sa + ty*ca;
    const float x2 = cx + rx*ca - by*sa, y2 = cy + rx*sa + by*ca;
    const float x3 = cx + lx*ca - by*sa, y3 = cy + lx*sa + by*ca;
    TexCoord2f(u0,v0); Vertex2f(x0,y0);
    TexCoord2f(u1,v0); Vertex2f(x1,y1);
    TexCoord2f(u1,v1); Vertex2f(x2,y2);
    TexCoord2f(u0,v0); Vertex2f(x0,y0);
    TexCoord2f(u1,v1); Vertex2f(x2,y2);
    TexCoord2f(u0,v1); Vertex2f(x3,y3);
}

void RenderBatch::NineSlice(unsigned int textureId,
                            float x, float y, float width, float height,
                            float borderLeft, float borderTop, float borderRight, float borderBottom,
                            float texWidth, float texHeight)
{
    const float sw = (texWidth  > 0.0f) ? texWidth  : 1.0f;
    const float sh = (texHeight > 0.0f) ? texHeight : 1.0f;
    if (borderLeft + borderRight > width)   { borderLeft = borderRight = width * 0.5f; }
    if (borderTop  + borderBottom > height) { borderTop = borderBottom = height * 0.5f; }

    const float uL  = 0.0f, uML = borderLeft / sw, uMR = (sw - borderRight)  / sw, uR  = 1.0f;
    const float vT  = 0.0f, vMT = borderTop  / sh, vMB = (sh - borderBottom) / sh, vB  = 1.0f;
    const float pxL = x, pxML = x + borderLeft, pxMR = x + width - borderRight,  pxR = x + width;
    const float pyT = y, pyMT = y + borderTop,  pyMB = y + height - borderBottom, pyB = y + height;
    (void)pyB; (void)pxR;

    SetTexture(textureId);
    auto slice = [&](float sx, float sy, float sw2, float sh2,
                     float su0, float sv0, float su1, float sv1)
    {
        if (sw2 <= 0.0f || sh2 <= 0.0f) return;
        SetMode(QUAD);
        TexCoord2f(su0,sv0); Vertex2f(sx,     sy);
        TexCoord2f(su1,sv0); Vertex2f(sx+sw2, sy);
        TexCoord2f(su1,sv1); Vertex2f(sx+sw2, sy+sh2);
        TexCoord2f(su0,sv1); Vertex2f(sx,     sy+sh2);
    };
    slice(pxL,  pyT,  borderLeft,  borderTop,     uL,  vT,  uML, vMT);
    slice(pxML, pyT,  pxMR-pxML,   borderTop,     uML, vT,  uMR, vMT);
    slice(pxMR, pyT,  borderRight, borderTop,     uMR, vT,  uR,  vMT);
    slice(pxL,  pyMT, borderLeft,  pyMB-pyMT,     uL,  vMT, uML, vMB);
    slice(pxML, pyMT, pxMR-pxML,   pyMB-pyMT,     uML, vMT, uMR, vMB);
    slice(pxMR, pyMT, borderRight, pyMB-pyMT,     uMR, vMT, uR,  vMB);
    slice(pxL,  pyMB, borderLeft,  borderBottom,  uL,  vMB, uML, vB);
    slice(pxML, pyMB, pxMR-pxML,   borderBottom,  uML, vMB, uMR, vB);
    slice(pxMR, pyMB, borderRight, borderBottom,  uMR, vMB, uR,  vB);
}

// ===========================================================================
// Textured polygon / triangle
// ===========================================================================

void RenderBatch::TexturedPolygon(const Vec2 *points, int pointCount, unsigned int textureId)
{
    if (!points || pointCount < 3) return;
    float minX = points[0].x, maxX = points[0].x;
    float minY = points[0].y, maxY = points[0].y;
    float cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < pointCount; ++i)
    {
        minX = std::min(minX, points[i].x); maxX = std::max(maxX, points[i].x);
        minY = std::min(minY, points[i].y); maxY = std::max(maxY, points[i].y);
        cx += points[i].x; cy += points[i].y;
    }
    const float w = std::max(0.0001f, maxX - minX);
    const float h = std::max(0.0001f, maxY - minY);
    cx /= (float)pointCount; cy /= (float)pointCount;
    const float cu = (cx - minX) / w, cv = (cy - minY) / h;

    SetTexture(textureId); SetMode(TRIANGLES);
    for (int i = 0; i < pointCount; ++i)
    {
        const int next = (i + 1) % pointCount;
        TexCoord2f(cu, cv); Vertex2f(cx, cy);
        TexCoord2f((points[i].x - minX) / w,    (points[i].y - minY) / h);
        Vertex2f(points[i].x, points[i].y);
        TexCoord2f((points[next].x - minX) / w, (points[next].y - minY) / h);
        Vertex2f(points[next].x, points[next].y);
    }
}

void RenderBatch::TexturedPolygonCustomUV(const TexVertex *verts, int vertexCount, unsigned int textureId)
{
    if (!verts || vertexCount < 3) return;
    float cx = 0, cy = 0, cu = 0, cv = 0;
    for (int i = 0; i < vertexCount; ++i)
    {
        cx += verts[i].position.x; cy += verts[i].position.y;
        cu += verts[i].texCoord.x; cv += verts[i].texCoord.y;
    }
    cx /= vertexCount; cy /= vertexCount;
    cu /= vertexCount; cv /= vertexCount;
    SetTexture(textureId); SetMode(TRIANGLES);
    for (int i = 0; i < vertexCount; ++i)
    {
        const int next = (i + 1) % vertexCount;
        TexCoord2f(cu, cv); Vertex2f(cx, cy);
        TexCoord2f(verts[i].texCoord.x,    verts[i].texCoord.y);
        Vertex2f(verts[i].position.x,   verts[i].position.y);
        TexCoord2f(verts[next].texCoord.x, verts[next].texCoord.y);
        Vertex2f(verts[next].position.x,  verts[next].position.y);
    }
}

void RenderBatch::TexturedQuad(const Vec2 &p1, const Vec2 &p2,
                               const Vec2 &p3, const Vec2 &p4,
                               unsigned int textureId)
{
    TexturedQuad(p1, p2, p3, p4,
                 {0.0f,0.0f}, {1.0f,0.0f}, {1.0f,1.0f}, {0.0f,1.0f},
                 textureId);
}

void RenderBatch::TexturedQuad(const Vec2 &p1, const Vec2 &p2,
                               const Vec2 &p3, const Vec2 &p4,
                               const Vec2 &uv1, const Vec2 &uv2,
                               const Vec2 &uv3, const Vec2 &uv4,
                               unsigned int textureId)
{
    SetTexture(textureId); SetMode(TRIANGLES);
    TexCoord2f(uv1.x,uv1.y); Vertex2f(p1.x,p1.y);
    TexCoord2f(uv2.x,uv2.y); Vertex2f(p2.x,p2.y);
    TexCoord2f(uv3.x,uv3.y); Vertex2f(p3.x,p3.y);
    TexCoord2f(uv1.x,uv1.y); Vertex2f(p1.x,p1.y);
    TexCoord2f(uv3.x,uv3.y); Vertex2f(p3.x,p3.y);
    TexCoord2f(uv4.x,uv4.y); Vertex2f(p4.x,p4.y);
}

void RenderBatch::TexturedTriangle(const Vec2 &p1, const Vec2 &p2,
                                   const Vec2 &p3, unsigned int textureId)
{
    TexturedTriangle(p1, p2, p3, {0.0f,0.0f}, {1.0f,0.0f}, {0.5f,1.0f}, textureId);
}

void RenderBatch::TexturedTriangle(const Vec2 &p1, const Vec2 &p2,
                                   const Vec2 &p3,
                                   const Vec2 &uv1, const Vec2 &uv2,
                                   const Vec2 &uv3, unsigned int textureId)
{
    SetTexture(textureId); SetMode(TRIANGLES);
    TexCoord2f(uv1.x,uv1.y); Vertex2f(p1.x,p1.y);
    TexCoord2f(uv2.x,uv2.y); Vertex2f(p2.x,p2.y);
    TexCoord2f(uv3.x,uv3.y); Vertex2f(p3.x,p3.y);
}

// ===========================================================================
// Splines
// ===========================================================================

void RenderBatch::BezierQuadratic(const Vec2 &p0, const Vec2 &p1, const Vec2 &p2, int segments)
{
    segments = std::max(1, segments);
    SetTexture(0u); SetMode(LINES);
    for (int i = 0; i < segments; ++i)
    {
        const float t1 = (float)i / segments, t2 = (float)(i+1) / segments;
        const float mt1 = 1.0f - t1, mt2 = 1.0f - t2;
        const float x1 = mt1*mt1*p0.x + 2*mt1*t1*p1.x + t1*t1*p2.x;
        const float y1 = mt1*mt1*p0.y + 2*mt1*t1*p1.y + t1*t1*p2.y;
        const float x2 = mt2*mt2*p0.x + 2*mt2*t2*p1.x + t2*t2*p2.x;
        const float y2 = mt2*mt2*p0.y + 2*mt2*t2*p1.y + t2*t2*p2.y;
        Vertex2f(x1, y1); Vertex2f(x2, y2);
    }
}

void RenderBatch::BezierCubic(const Vec2 &p0, const Vec2 &p1,
                              const Vec2 &p2, const Vec2 &p3, int segments)
{
    segments = std::max(1, segments);
    SetTexture(0u); SetMode(LINES);
    for (int i = 0; i < segments; ++i)
    {
        const float t1 = (float)i / segments, t2 = (float)(i+1) / segments;
        const float mt1 = 1.0f - t1, mt2 = 1.0f - t2;
        const float x1 = mt1*mt1*mt1*p0.x + 3*mt1*mt1*t1*p1.x + 3*mt1*t1*t1*p2.x + t1*t1*t1*p3.x;
        const float y1 = mt1*mt1*mt1*p0.y + 3*mt1*mt1*t1*p1.y + 3*mt1*t1*t1*p2.y + t1*t1*t1*p3.y;
        const float x2 = mt2*mt2*mt2*p0.x + 3*mt2*mt2*t2*p1.x + 3*mt2*t2*t2*p2.x + t2*t2*t2*p3.x;
        const float y2 = mt2*mt2*mt2*p0.y + 3*mt2*mt2*t2*p1.y + 3*mt2*t2*t2*p2.y + t2*t2*t2*p3.y;
        Vertex2f(x1, y1); Vertex2f(x2, y2);
    }
}

void RenderBatch::CatmullRomSpline(const Vec2 *points, int pointCount, int segments)
{
    if (!points || pointCount < 4) return;
    segments = std::max(1, segments);
    SetTexture(0u); SetMode(LINES);
    for (int i = 0; i < pointCount - 3; ++i)
    {
        const Vec2 &p0 = points[i], &p1 = points[i+1],
                        &p2 = points[i+2], &p3 = points[i+3];
        for (int s = 0; s < segments; ++s)
        {
            float t1 = (float)s / segments, t2 = (float)(s+1) / segments;
            float t1_2 = t1*t1, t1_3 = t1_2*t1, t2_2 = t2*t2, t2_3 = t2_2*t2;
            const float x1 = 0.5f*((2*p1.x)+(-p0.x+p2.x)*t1+(2*p0.x-5*p1.x+4*p2.x-p3.x)*t1_2+(-p0.x+3*p1.x-3*p2.x+p3.x)*t1_3);
            const float y1 = 0.5f*((2*p1.y)+(-p0.y+p2.y)*t1+(2*p0.y-5*p1.y+4*p2.y-p3.y)*t1_2+(-p0.y+3*p1.y-3*p2.y+p3.y)*t1_3);
            const float x2 = 0.5f*((2*p1.x)+(-p0.x+p2.x)*t2+(2*p0.x-5*p1.x+4*p2.x-p3.x)*t2_2+(-p0.x+3*p1.x-3*p2.x+p3.x)*t2_3);
            const float y2 = 0.5f*((2*p1.y)+(-p0.y+p2.y)*t2+(2*p0.y-5*p1.y+4*p2.y-p3.y)*t2_2+(-p0.y+3*p1.y-3*p2.y+p3.y)*t2_3);
            Vertex2f(x1, y1); Vertex2f(x2, y2);
        }
    }
}

void RenderBatch::BSpline(const Vec2 *points, int pointCount, int segments, int /*degree*/)
{
    if (!points || pointCount < 4) return;
    segments = std::max(1, segments);
    SetTexture(0u); SetMode(LINES);
    for (int i = 0; i < pointCount - 3; ++i)
    {
        const Vec2 &p0 = points[i], &p1 = points[i+1],
                        &p2 = points[i+2], &p3 = points[i+3];
        for (int s = 0; s < segments; ++s)
        {
            float t1 = (float)s / segments, t2 = (float)(s+1) / segments;
            float t1_2=t1*t1, t1_3=t1_2*t1, t2_2=t2*t2, t2_3=t2_2*t2;
            const float b0_1=(1-3*t1+3*t1_2-t1_3)/6, b1_1=(4-6*t1_2+3*t1_3)/6, b2_1=(1+3*t1+3*t1_2-3*t1_3)/6, b3_1=t1_3/6;
            const float b0_2=(1-3*t2+3*t2_2-t2_3)/6, b1_2=(4-6*t2_2+3*t2_3)/6, b2_2=(1+3*t2+3*t2_2-3*t2_3)/6, b3_2=t2_3/6;
            const float x1=b0_1*p0.x+b1_1*p1.x+b2_1*p2.x+b3_1*p3.x, y1=b0_1*p0.y+b1_1*p1.y+b2_1*p2.y+b3_1*p3.y;
            const float x2=b0_2*p0.x+b1_2*p1.x+b2_2*p2.x+b3_2*p3.x, y2=b0_2*p0.y+b1_2*p1.y+b2_2*p2.y+b3_2*p3.y;
            Vertex2f(x1, y1); Vertex2f(x2, y2);
        }
    }
}

void RenderBatch::HermiteSpline(const HermitePoint *points, int pointCount, int segments)
{
    if (!points || pointCount < 2) return;
    segments = std::max(1, segments);
    SetTexture(0u); SetMode(LINES);
    for (int i = 0; i < pointCount - 1; ++i)
    {
        const Vec2 &p0 = points[i].position,   &m0 = points[i].tangent;
        const Vec2 &p1 = points[i+1].position, &m1 = points[i+1].tangent;
        for (int s = 0; s < segments; ++s)
        {
            float t1=float(s)/segments, t2=float(s+1)/segments;
            float t1_2=t1*t1, t1_3=t1_2*t1, t2_2=t2*t2, t2_3=t2_2*t2;
            const float h00_1=2*t1_3-3*t1_2+1, h10_1=t1_3-2*t1_2+t1, h01_1=-2*t1_3+3*t1_2, h11_1=t1_3-t1_2;
            const float h00_2=2*t2_3-3*t2_2+1, h10_2=t2_3-2*t2_2+t2, h01_2=-2*t2_3+3*t2_2, h11_2=t2_3-t2_2;
            const float x1=h00_1*p0.x+h10_1*m0.x+h01_1*p1.x+h11_1*m1.x;
            const float y1=h00_1*p0.y+h10_1*m0.y+h01_1*p1.y+h11_1*m1.y;
            const float x2=h00_2*p0.x+h10_2*m0.x+h01_2*p1.x+h11_2*m1.x;
            const float y2=h00_2*p0.y+h10_2*m0.y+h01_2*p1.y+h11_2*m1.y;
            Vertex2f(x1, y1); Vertex2f(x2, y2);
        }
    }
}

void RenderBatch::ThickSpline(const Vec2 *points, int pointCount, float thickness, int /*segments*/)
{
    if (!points || pointCount < 2) return;
    SetTexture(0u); SetMode(TRIANGLES);
    const float half = thickness * 0.5f;
    for (int i = 0; i < pointCount - 1; ++i)
    {
        float dx = points[i+1].x - points[i].x;
        float dy = points[i+1].y - points[i].y;
        const float len = sqrtf(dx*dx + dy*dy);
        if (len <= 0.0001f) continue;
        dx /= len; dy /= len;
        const float px = -dy * half, py = dx * half;
        const Vec2 &pt0 = points[i], &pt1 = points[i+1];
        Vertex2f(pt0.x+px, pt0.y+py); Vertex2f(pt0.x-px, pt0.y-py); Vertex2f(pt1.x+px, pt1.y+py);
        Vertex2f(pt0.x-px, pt0.y-py); Vertex2f(pt1.x-px, pt1.y-py); Vertex2f(pt1.x+px, pt1.y+py);
    }
}

// ===========================================================================
// 3D Shapes
// ===========================================================================

void RenderBatch::Line3D(float x0, float y0, float z0, float x1, float y1, float z1)
{
    SetTexture(0u); SetMode(LINES);
    Vertex3f(x0, y0, z0);
    Vertex3f(x1, y1, z1);
}

void RenderBatch::Line3D(const Vec3 &start, const Vec3 &end)
{
    Line3D(start.x, start.y, start.z, end.x, end.y, end.z);
}

void RenderBatch::Triangle(const Vec3 &p1, const Vec3 &p2, const Vec3 &p3)
{
    SetTexture(0u); SetMode(TRIANGLES);
    Vertex3f(p1.x, p1.y, p1.z);
    Vertex3f(p2.x, p2.y, p2.z);
    Vertex3f(p3.x, p3.y, p3.z);
}

void RenderBatch::Triangle(const Vec3 &p1, const Vec3 &p2, const Vec3 &p3,
                           const Vec2 &t1, const Vec2 &t2, const Vec2 &t3)
{
    SetMode(TRIANGLES);
    TexCoord2f(t1.x, t1.y); Vertex3f(p1.x, p1.y, p1.z);
    TexCoord2f(t2.x, t2.y); Vertex3f(p2.x, p2.y, p2.z);
    TexCoord2f(t3.x, t3.y); Vertex3f(p3.x, p3.y, p3.z);
}

void RenderBatch::TriangleLines(const Vec3 &p1, const Vec3 &p2, const Vec3 &p3)
{
    SetTexture(0u);
    Line3D(p1, p2); Line3D(p2, p3); Line3D(p3, p1);
}

void RenderBatch::Box(const BoundingBox &box)
{
    SetTexture(0u);
    const Vec3 &mn = box.min, &mx = box.max;
    Line3D(mn.x,mn.y,mn.z, mx.x,mn.y,mn.z); Line3D(mx.x,mn.y,mn.z, mx.x,mx.y,mn.z);
    Line3D(mx.x,mx.y,mn.z, mn.x,mx.y,mn.z); Line3D(mn.x,mx.y,mn.z, mn.x,mn.y,mn.z);
    Line3D(mn.x,mn.y,mx.z, mx.x,mn.y,mx.z); Line3D(mx.x,mn.y,mx.z, mx.x,mx.y,mx.z);
    Line3D(mx.x,mx.y,mx.z, mn.x,mx.y,mx.z); Line3D(mn.x,mx.y,mx.z, mn.x,mn.y,mx.z);
    Line3D(mn.x,mn.y,mn.z, mn.x,mn.y,mx.z); Line3D(mx.x,mn.y,mn.z, mx.x,mn.y,mx.z);
    Line3D(mx.x,mx.y,mn.z, mx.x,mx.y,mx.z); Line3D(mn.x,mx.y,mn.z, mn.x,mx.y,mx.z);
}

void RenderBatch::Box(const BoundingBox &box, const Mat4&transform)
{
    BeginTransform(transform);
    Box(box);
    EndTransform();
}

void RenderBatch::Cube(const Vec3 &position, float w, float h, float d, bool wire)
{
    SetTexture(0u);
    const BoundingBox box{
        Vec3(position.x - w*0.5f, position.y - h*0.5f, position.z - d*0.5f),
        Vec3(position.x + w*0.5f, position.y + h*0.5f, position.z + d*0.5f)};

    if (wire) { Box(box); return; }

    const Vec3 p000(box.min.x,box.min.y,box.min.z), p001(box.min.x,box.min.y,box.max.z);
    const Vec3 p010(box.min.x,box.max.y,box.min.z), p011(box.min.x,box.max.y,box.max.z);
    const Vec3 p100(box.max.x,box.min.y,box.min.z), p101(box.max.x,box.min.y,box.max.z);
    const Vec3 p110(box.max.x,box.max.y,box.min.z), p111(box.max.x,box.max.y,box.max.z);

    Triangle(p001,p101,p111); Triangle(p001,p111,p011);
    Triangle(p100,p000,p010); Triangle(p100,p010,p110);
    Triangle(p000,p001,p011); Triangle(p000,p011,p010);
    Triangle(p101,p100,p110); Triangle(p101,p110,p111);
    Triangle(p010,p011,p111); Triangle(p010,p111,p110);
    Triangle(p000,p100,p101); Triangle(p000,p101,p001);
}

void RenderBatch::Sphere(const Vec3 &position, float radius, int rings, int slices, bool wire)
{
    rings  = std::max(rings,  3);
    slices = std::max(slices, 4);
    SetTexture(0u);

    if (wire) SetMode(LINES);
    else      SetMode(TRIANGLES);

    for (int i = 0; i < rings; ++i)
    {
        const float theta1 = (float)i     * PI / (float)rings;
        const float theta2 = (float)(i+1) * PI / (float)rings;
        for (int j = 0; j < slices; ++j)
        {
            const float phi1 = (float)j     * 2.0f * PI / (float)slices;
            const float phi2 = (float)(j+1) * 2.0f * PI / (float)slices;
            const Vec3 v1(position.x + radius*sinf(theta1)*cosf(phi1),
                               position.y + radius*cosf(theta1),
                               position.z + radius*sinf(theta1)*sinf(phi1));
            const Vec3 v2(position.x + radius*sinf(theta1)*cosf(phi2),
                               position.y + radius*cosf(theta1),
                               position.z + radius*sinf(theta1)*sinf(phi2));
            const Vec3 v3(position.x + radius*sinf(theta2)*cosf(phi1),
                               position.y + radius*cosf(theta2),
                               position.z + radius*sinf(theta2)*sinf(phi1));
            const Vec3 v4(position.x + radius*sinf(theta2)*cosf(phi2),
                               position.y + radius*cosf(theta2),
                               position.z + radius*sinf(theta2)*sinf(phi2));
            if (wire) { Line3D(v1, v2); Line3D(v1, v3); }
            else      { Triangle(v1, v2, v3); Triangle(v2, v4, v3); }
        }
    }
}

void RenderBatch::Cone(const Vec3 &position, float radius, float height, int segments, bool wire)
{
    segments = std::max(segments, 3);
    SetTexture(0u);
    const Vec3 apex(position.x, position.y + height * 0.5f, position.z);
    const float baseY = position.y - height * 0.5f;

    if (wire) SetMode(LINES);
    else      SetMode(TRIANGLES);

    for (int i = 0; i < segments; ++i)
    {
        const float a0 = (float)i     * 2.0f * PI / (float)segments;
        const float a1 = (float)(i+1) * 2.0f * PI / (float)segments;
        const Vec3 b0(position.x + cosf(a0)*radius, baseY, position.z + sinf(a0)*radius);
        const Vec3 b1(position.x + cosf(a1)*radius, baseY, position.z + sinf(a1)*radius);
        if (wire) { Line3D(b0, b1); Line3D(apex, b0); }
        else
        {
            Triangle(apex, b0, b1);
            Triangle(Vec3(position.x, baseY, position.z), b1, b0);
        }
    }
}

void RenderBatch::Cylinder(const Vec3 &position, float radius, float height, int segments, bool wire)
{
    segments = std::max(segments, 3);
    SetTexture(0u);
    const float y0 = position.y - height * 0.5f;
    const float y1 = position.y + height * 0.5f;

    if (wire) SetMode(LINES);
    else      SetMode(TRIANGLES);

    for (int i = 0; i < segments; ++i)
    {
        const float a0 = (float)i     * 2.0f * PI / (float)segments;
        const float a1 = (float)(i+1) * 2.0f * PI / (float)segments;
        const Vec3 b0(position.x + cosf(a0)*radius, y0, position.z + sinf(a0)*radius);
        const Vec3 b1(position.x + cosf(a1)*radius, y0, position.z + sinf(a1)*radius);
        const Vec3 t0(position.x + cosf(a0)*radius, y1, position.z + sinf(a0)*radius);
        const Vec3 t1(position.x + cosf(a1)*radius, y1, position.z + sinf(a1)*radius);
        if (wire) { Line3D(b0, b1); Line3D(t0, t1); Line3D(b0, t0); }
        else
        {
            Triangle(b0, b1, t1); Triangle(b0, t1, t0);
            Triangle(Vec3(position.x, y0, position.z), b1, b0);
            Triangle(Vec3(position.x, y1, position.z), t0, t1);
        }
    }
}

void RenderBatch::Capsule(const Vec3 &position, float radius, float height, int segments, bool wire)
{
    const float cylH = std::max(0.0f, height - radius * 2.0f);
    Cylinder(position, radius, cylH, segments, wire);
    Sphere(Vec3(position.x, position.y + cylH * 0.5f, position.z),
           radius, std::max(segments / 2, 4), segments, wire);
    Sphere(Vec3(position.x, position.y - cylH * 0.5f, position.z),
           radius, std::max(segments / 2, 4), segments, wire);
}

void RenderBatch::Circle3D(const Vec3 &center, float radius, const Vec3 &normal, int segments)
{
    if (segments < 3) segments = 32;
    SetTexture(0u); SetMode(LINES);

    Vec3 tangent;
    if (fabsf(normal.x) > 0.9f)
        tangent = Vec3::Cross(Vec3(0,1,0), normal).normalized();
    else
        tangent = Vec3::Cross(Vec3(1,0,0), normal).normalized();
    const Vec3 bitangent = Vec3::Cross(normal, tangent).normalized();
    const float step = 2.0f * PI / (float)segments;

    for (int i = 0; i < segments; ++i)
    {
        const float a1 = step * i, a2 = step * (i + 1);
        const Vec3 p1 = center + (tangent * cosf(a1) + bitangent * sinf(a1)) * radius;
        const Vec3 p2 = center + (tangent * cosf(a2) + bitangent * sinf(a2)) * radius;
        Vertex3f(p1.x, p1.y, p1.z);
        Vertex3f(p2.x, p2.y, p2.z);
    }
}

void RenderBatch::CircleXZ(const Vec3 &center, float radius, int segments)
{
    if (segments < 3) segments = 32;
    SetTexture(0u); SetMode(LINES);
    const float step = 2.0f * PI / (float)segments;
    for (int i = 0; i < segments; ++i)
    {
        const float a1 = step * i, a2 = step * (i + 1);
        Vertex3f(center.x + cosf(a1)*radius, center.y, center.z + sinf(a1)*radius);
        Vertex3f(center.x + cosf(a2)*radius, center.y, center.z + sinf(a2)*radius);
    }
}

void RenderBatch::Grid(int slices, float spacing, bool axes)
{
    slices = std::max(slices, 1);
    SetTexture(0u); SetMode(LINES);
    const float half = (float)slices * spacing * 0.5f;

    for (int i = 0; i <= slices; ++i)
    {
        const float offset = -half + (float)i * spacing;
        if (axes && fabsf(offset) < 0.0001f)
            SetColor(0.8f, 0.2f, 0.2f);
        else
            SetColor(0.45f, 0.45f, 0.45f);
        Line3D(offset, 0.0f, -half,  offset, 0.0f,  half);

        if (axes && fabsf(offset) < 0.0001f)
            SetColor(0.2f, 0.8f, 0.2f);
        else
            SetColor(0.45f, 0.45f, 0.45f);
        Line3D(-half, 0.0f, offset,  half, 0.0f, offset);
    }
    if (axes) SetColor(1.0f, 1.0f, 1.0f);
}
