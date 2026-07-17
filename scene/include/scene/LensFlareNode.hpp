#pragma once

#include "scene/Node3D.hpp"
#include <coregl/gl_buffer.hpp>
#include <coregl/gl_renderer.hpp> // gl::Query
#include <coregl/gl_shader.hpp>
#include <coregl/gl_texture.hpp>
#include <coregl/gl_vertex_array.hpp>
#include <vector>

class Camera3D;

class LensFlareNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_LENSFLARE;

    struct Element
    {
        float position = 0.f;
        float size     = 0.05f;
        Vec3  color{1, 1, 1};
        float alpha    = 0.8f;
        Vec4  pixelRect{0, 0, 0, 0};
    };

    explicit LensFlareNode(const std::string& name = "LensFlareNode");
    ~LensFlareNode() override;

    bool is_a(NodeType t) const override { return t == NT_LENSFLARE || Node3D::is_a(t); }

    void set_enabled(bool e)  { m_enabled = e; }
    bool is_enabled() const   { return m_enabled; }
    void set_sun_color(const Vec3& c) { m_sunColor = c; }

    void add_flare(const Element& e) { m_flares.push_back(e); }
    void clear_flares()              { m_flares.clear(); }
    void init_default_flares();

    bool ensure_gpu();
    u32  build(Camera3D& cam, const Vec3& sunDir);
    void release_gpu();

    gl::VertexArray& vao()          { return m_vao; }
    u32              index_count() const { return m_indexCount; }
    gl::Shader*      shader() const { return m_shader; }
    gl::Texture&     tex()          { return m_tex; }

protected:
    void _release_gpu() override { release_gpu(); }

private:
    struct FVert { float x, y, z; float u, v; float r, g, b, a; };

    Vec3 m_sunColor{1, 1, 1};
    bool m_enabled = true;

    float m_atlasW = 256.f, m_atlasH = 256.f;
    std::vector<Element> m_flares;

    gl::Buffer      m_vbo, m_ibo;
    gl::VertexArray m_vao;
    gl::Shader*     m_shader = nullptr;
    bool            m_gpuReady = false;
    u32             m_indexCount = 0;
    u32             m_capacity = 0;

    gl::Texture m_tex;
    bool        m_texLoaded = false;

    std::vector<FVert> m_scratchVerts;
    std::vector<u32>   m_scratchIdx;

    // hardware occlusion query: is anything already in the depth buffer
    // between the camera and the sun's screen position? A tiny quad is
    // drawn (color/depth-write off) at the sun's NDC xy with depth tested
    // against the scene already rendered this frame; ANY_SAMPLES_PASSED
    // tells us if it was blocked. Result lags a frame or two (GPU latency,
    // read back via IsReady() so we never stall the pipeline), so the
    // fade factor is smoothed toward the target instead of snapping.
    gl::Query       m_occQuery;
    bool            m_occQueryPending = false;
    float           m_visibility = 1.f;
    gl::Buffer      m_occVbo, m_occIbo;
    gl::VertexArray m_occVao;
    gl::Shader*     m_occShader = nullptr;

    float computeFade(const Vec2& ndc) const;
    void  buildGeometry(const Vec2& sunNDC, float fade, float aspect);
    float updateOcclusion(const Vec2& sunNDC, float aspect);
};
