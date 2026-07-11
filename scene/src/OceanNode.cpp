#include "scene/OceanNode.hpp"

// ── procedural bump (normal map) ──────────────────────────────────────────
// 128×128 RG texture: rg = tangent-space normal xy encoded as (n+1)/2.
// A mix of sine waves at different frequencies/directions gives a watery
// dudley-like perturbation pattern that tiles seamlessly.

static void generateBumpTexture(gl::Texture& tex)
{
    const int W = 128, H = 128;
    unsigned char data[W * H * 4];

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            float fx = (float)x / W;
            float fy = (float)y / H;

            // two layers of waves in different directions → rich perturbation
            float n1 = sinf(fx * 6.2832f * 3.0f + fy * 6.2832f * 1.7f) * 0.5f;
            float n2 = sinf(fx * 6.2832f * 1.3f - fy * 6.2832f * 2.9f) * 0.5f;
            float n3 = sinf((fx + fy) * 6.2832f * 5.0f) * 0.25f;
            float nx = n1 + n3;
            float ny = n2 - n3;
            // z is derived; keep the normal roughly unit-length
            float nz = 1.0f - 0.3f * (fabsf(nx) + fabsf(ny));

            int idx = (y * W + x) * 4;
            data[idx + 0] = (unsigned char)((nx * 0.5f + 0.5f) * 255.0f); // R = normal.x
            data[idx + 1] = (unsigned char)((ny * 0.5f + 0.5f) * 255.0f); // G = normal.y
            data[idx + 2] = (unsigned char)((nz * 0.5f + 0.5f) * 255.0f); // B = normal.z
            data[idx + 3] = 255;
        }
    }

    tex.Load2D(data, W, H, gl::TextureFormat::RGBA8);
    tex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    tex.SetWrap(gl::TextureWrap::REPEAT, gl::TextureWrap::REPEAT);
}

// ── procedural foam (white noise) ─────────────────────────────────────────
// 128×128 R8 texture: value-noise smoothed with a cheap box blur so the
// foam pattern isn't pure salt-and-pepper. The shader reads .r as coverage.

 

static float smoothNoise(int x, int y)
{
    // hash-based pseudo-random [0,1)
    unsigned n = (unsigned)(x * 374761393 + y * 668265263);
    n = (n ^ (n >> 13)) * 1274126177u;
    n = n ^ (n >> 16);
    return (float)(n & 0xFFFFFF) / (float)0x1000000;
}

static float valueNoise(float x, float y)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float v00 = smoothNoise(ix, iy);
    float v10 = smoothNoise(ix + 1, iy);
    float v01 = smoothNoise(ix, iy + 1);
    float v11 = smoothNoise(ix + 1, iy + 1);
    return v00 + (v10 - v00) * sx + ((v01 - v00) + (v11 - v10 - v01 + v00) * sx) * sy;
}

static void generateFoamTexture(gl::Texture& tex)
{
    const int W = 128, H = 128;
    unsigned char data[W * H];

    // multi-octave value noise → clumps of bright/dark
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            float v = 0.0f;
            v += valueNoise((float)x / W * 4.0f, (float)y / H * 4.0f) * 0.5f;
            v += valueNoise((float)x / W * 8.0f, (float)y / H * 8.0f) * 0.25f;
            v += valueNoise((float)x / W * 16.0f, (float)y / H * 16.0f) * 0.125f;
            v += valueNoise((float)x / W * 32.0f, (float)y / H * 32.0f) * 0.0625f;
            v = v / 0.9375f; // normalize to ~[0,1]
            data[y * W + x] = (unsigned char)(v * 255.0f);
        }
    }

    tex.Load2D(data, W, H, gl::TextureFormat::R8);
    tex.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    tex.SetWrap(gl::TextureWrap::REPEAT, gl::TextureWrap::REPEAT);
}

void OceanNode::ensure_textures()
{
    if (m_texturesReady) return;
    generateBumpTexture(m_builtinBump);
    generateFoamTexture(m_builtinFoam);
    m_texturesReady = true;
}

void OceanNode::release_textures()
{
    m_builtinBump.Release();
    m_builtinFoam.Release();
    m_texturesReady = false;
}

void OceanNode::build_surface(Mesh& mesh)
{
    // dense flat grid: the ocean shader displaces every vertex, so the
    // resolution decides how sharp the waves can be
    const int cells = grid_resolution;
    const int n = cells + 1;
    const float half = get_size();
    const float step = (2.f * half) / cells;

    std::vector<MeshVertex> verts((size_t)n * n);
    for (int j = 0; j < n; ++j)
    {
        for (int i = 0; i < n; ++i)
        {
            MeshVertex& v = verts[(size_t)j * n + i];
            v.position = Vec3(-half + i * step, 0.f, -half + j * step);
            v.normal = Vec3(0.f, 1.f, 0.f);
            v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            // uv in world units so the bump tiling is size-independent
            v.uv = Vec2(v.position.x, v.position.z);
        }
    }

    std::vector<u32> idx;
    idx.reserve((size_t)cells * cells * 6);
    for (int j = 0; j < cells; ++j)
    {
        for (int i = 0; i < cells; ++i)
        {
            u32 a = (u32)(j * n + i);
            u32 b = a + 1;
            u32 d = a + (u32)n;
            u32 c = d + 1;
            // CCW seen from above
            idx.push_back(a);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(a);
            idx.push_back(d);
            idx.push_back(c);
        }
    }

    mesh.set_data(verts.data(), (u32)verts.size(), idx.data(), (u32)idx.size());
}
