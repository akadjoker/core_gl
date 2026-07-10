#pragma once

#include "scene/Math.hpp"
#include <coregl/gl_buffer.hpp>
#include <coregl/gl_shader.hpp>
#include <coregl/gl_texture.hpp>
#include <coregl/gl_vertex_array.hpp>
#include <vector>

class Camera3D;

// ── Lens flare: screen-space quads along the sun→screen-center axis ──
//
// Owns its VBO/IBO/VAO directly (no Mesh, since the vertex format is
// simpler: pos3 + uv2 + color4).  SceneRenderer binds the shader/texture
// and draws via vao() + index_count().

class LensFlare
{
public:
    struct Element
    {
        float position = 0.f;
        float size     = 0.05f;
        Vec3  color{1, 1, 1};
        float alpha    = 0.8f;
        Vec4  pixelRect{0, 0, 0, 0};
    };

    LensFlare() = default;
    ~LensFlare();

    void set_sun_direction(const Vec3& d) { m_sunDir = d.normalized(); }
    void set_sun_color(const Vec3& c)     { m_sunColor = c; }
    void set_enabled(bool e)              { m_enabled = e; }
    bool is_enabled() const               { return m_enabled; }

    void add_flare(const Element& e) { m_flares.push_back(e); }
    void clear_flares()              { m_flares.clear(); }
    void init_default_flares();

    // ── renderer-side ──
    bool ensure_gpu();
    u32  build(Camera3D& cam);
    void release_gpu();

    gl::VertexArray& vao()          { return m_vao; }
    u32              index_count() const { return m_indexCount; }
    gl::Shader*      shader() const { return m_shader; }

private:
    struct FVert { float x, y, z; float u, v; float r, g, b, a; };

    Vec3 m_sunDir{0, -1, 0}, m_sunColor{1, 1, 1};
    bool m_enabled = true;

    float m_atlasW = 256.f, m_atlasH = 256.f;
    std::vector<Element> m_flares;

    gl::Buffer     m_vbo, m_ibo;
    gl::VertexArray m_vao;
    gl::Shader*    m_shader = nullptr;
    bool           m_gpuReady = false;
    u32            m_indexCount = 0;
    u32            m_capacity = 0; // max quads allocated

    std::vector<FVert>  m_scratchVerts;
    std::vector<u32>    m_scratchIdx;

    float computeFade(const Vec2& ndc) const;
    void  buildGeometry(const Vec2& sunNDC, float fade, float aspect);
};
