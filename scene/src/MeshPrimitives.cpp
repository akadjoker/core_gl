#include "scene/MeshPrimitives.hpp"
#include "scene/Mesh.hpp"
#include <cmath>
#include <vector>

namespace primitives
{

static const float kPi = 3.14159265358979f;

// hands the arrays to the mesh with a single full-range surface
static void finish(Mesh& out, std::vector<MeshVertex>& verts, std::vector<u16>& idx)
{
    out.set_data(verts.data(), (u32)verts.size(), idx.data(), (u32)idx.size());
    out.compute_bounds();
    out.compute_tangents(); // real tangents from uv+normal, not the (1,0,0,1) placeholder
}

void cube(Mesh& out, float sx, float sy, float sz)
{
    const float x = sx * 0.5f, y = sy * 0.5f, z = sz * 0.5f;
    // 6 faces * 4 verts, flat normals
    const float n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const float p[6][4][3] = {
        {{-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z}},     // +Z
        {{x, -y, -z}, {-x, -y, -z}, {-x, y, -z}, {x, y, -z}}, // -Z
        {{x, -y, z}, {x, -y, -z}, {x, y, -z}, {x, y, z}},     // +X
        {{-x, -y, -z}, {-x, -y, z}, {-x, y, z}, {-x, y, -z}}, // -X
        {{-x, y, z}, {x, y, z}, {x, y, -z}, {-x, y, -z}},     // +Y
        {{-x, -y, -z}, {x, -y, -z}, {x, -y, z}, {-x, -y, z}}, // -Y
    };
    std::vector<MeshVertex> verts(24);
    std::vector<u16> idx;
    idx.reserve(36);
    for (int f = 0; f < 6; ++f)
    {
        for (int v = 0; v < 4; ++v)
        {
            MeshVertex& mv = verts[(size_t)f * 4 + v];
            mv.position = Vec3(p[f][v][0], p[f][v][1], p[f][v][2]);
            mv.normal = Vec3(n[f][0], n[f][1], n[f][2]);
            mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            mv.uv = Vec2((float)(v == 1 || v == 2), (float)(v >= 2));
        }
        const u16 b = (u16)(f * 4);
        const u16 quad[6] = {b, (u16)(b + 1), (u16)(b + 2), b, (u16)(b + 2), (u16)(b + 3)};
        idx.insert(idx.end(), quad, quad + 6);
    }
    finish(out, verts, idx);
}

void plane(Mesh& out, float width, float depth, float uvTiles, int segX, int segZ)
{
    segX = segX < 1 ? 1 : segX;
    segZ = segZ < 1 ? 1 : segZ;
    const int nx = segX + 1, nz = segZ + 1;
    const float x0 = -width * 0.5f, z0 = -depth * 0.5f;
    const float dx = width / (float)segX, dz = depth / (float)segZ;

    std::vector<MeshVertex> verts((size_t)nx * nz);
    for (int j = 0; j < nz; ++j)
    {
        for (int i = 0; i < nx; ++i)
        {
            MeshVertex& v = verts[(size_t)j * nx + i];
            v.position = Vec3(x0 + (float)i * dx, 0.f, z0 + (float)j * dz);
            v.normal = Vec3(0.f, 1.f, 0.f);
            v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            v.uv = Vec2((float)i / (float)segX * uvTiles, (float)j / (float)segZ * uvTiles);
        }
    }
    std::vector<u16> idx;
    idx.reserve((size_t)segX * segZ * 6);
    for (int j = 0; j < segZ; ++j)
        for (int i = 0; i < segX; ++i)
        {
            const u16 a = (u16)(j * nx + i), b = (u16)(a + 1);
            const u16 c = (u16)(a + nx), d = (u16)(c + 1);
            const u16 quad[6] = {a, c, b, b, c, d};
            idx.insert(idx.end(), quad, quad + 6);
        }
    finish(out, verts, idx);
}

void hills_plane(Mesh& out, float width, float depth, int segX, int segZ,
                 float (*heightFn)(float x, float z), float uvTiles)
{
    plane(out, width, depth, uvTiles, segX, segZ);
    // displace the flat plane's CPU data, then re-finish (bounds+tangents)
    std::vector<MeshVertex> verts = out.vertices();
    for (MeshVertex& v : verts)
        v.position.y = heightFn(v.position.x, v.position.z);

    // central-difference normals need neighbor heights; cheap here since
    // segX/segZ is small (this is a decorative ground, not open-world)
    const int nx = segX + 1;
    const float dx = width / (float)segX, dz = depth / (float)segZ;
    for (int j = 0; j < segZ + 1; ++j)
    {
        for (int i = 0; i < nx; ++i)
        {
            MeshVertex& v = verts[(size_t)j * nx + i];
            const float hl = heightFn(v.position.x - dx, v.position.z);
            const float hr = heightFn(v.position.x + dx, v.position.z);
            const float hd = heightFn(v.position.x, v.position.z - dz);
            const float hu = heightFn(v.position.x, v.position.z + dz);
            v.normal = Vec3(hl - hr, 2.f * ((dx + dz) * 0.5f), hd - hu).normalized();
        }
    }
    const std::vector<u32>& idx32 = out.indices();
    std::vector<u16> idx(idx32.begin(), idx32.end());
    finish(out, verts, idx);
}

void heightfield(Mesh& out, const float* heights, int w, int h, float cellSize, float uvTiles)
{
    std::vector<MeshVertex> verts((size_t)w * h);
    for (int j = 0; j < h; ++j)
    {
        for (int i = 0; i < w; ++i)
        {
            MeshVertex& v = verts[(size_t)j * w + i];
            v.position = Vec3((float)i * cellSize, heights[(size_t)j * w + i],
                              (float)j * cellSize);
            v.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            v.uv = Vec2((float)i / (float)(w - 1) * uvTiles, (float)j / (float)(h - 1) * uvTiles);
            const float hl = heights[(size_t)j * w + (i > 0 ? i - 1 : i)];
            const float hr = heights[(size_t)j * w + (i < w - 1 ? i + 1 : i)];
            const float hd = heights[(size_t)(j > 0 ? j - 1 : j) * w + i];
            const float hu = heights[(size_t)(j < h - 1 ? j + 1 : j) * w + i];
            v.normal = Vec3(hl - hr, 2.f * cellSize, hd - hu).normalized();
        }
    }
    std::vector<u16> idx;
    idx.reserve((size_t)(w - 1) * (h - 1) * 6);
    for (int j = 0; j < h - 1; ++j)
        for (int i = 0; i < w - 1; ++i)
        {
            const u16 a = (u16)(j * w + i), b = (u16)(a + 1);
            const u16 c = (u16)(a + w), d = (u16)(c + 1);
            const u16 quad[6] = {a, c, b, b, c, d};
            idx.insert(idx.end(), quad, quad + 6);
        }
    finish(out, verts, idx);
}

void sphere(Mesh& out, float radius, int rings, int slices)
{
    std::vector<MeshVertex> verts;
    std::vector<u16> idx;
    verts.reserve((size_t)(rings + 1) * (slices + 1));
    for (int r = 0; r <= rings; ++r)
    {
        const float v = (float)r / (float)rings;
        const float phi = v * kPi; // 0 = top pole
        for (int s = 0; s <= slices; ++s)
        {
            const float u = (float)s / (float)slices;
            const float theta = u * 2.f * kPi;
            const Vec3 n(sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta));
            MeshVertex mv;
            mv.position = n * radius;
            mv.normal = n;
            mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            mv.uv = Vec2(u, v);
            verts.push_back(mv);
        }
    }
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < slices; ++s)
        {
            const u16 a = (u16)(r * (slices + 1) + s);
            const u16 b = (u16)(a + slices + 1);
            const u16 quad[6] = {a, (u16)(a + 1), b, (u16)(a + 1), (u16)(b + 1), b};
            idx.insert(idx.end(), quad, quad + 6);
        }
    }
    finish(out, verts, idx);
}

// shared side+caps builder: coneScale=1 cylinder, 0 cone (apex ring)
static void tube(Mesh& out, float radius, float height, int slices, float topScale)
{
    std::vector<MeshVertex> verts;
    std::vector<u16> idx;
    // side normals lean outward for cones: slope = radius shrink over height
    const float slope = (radius - radius * topScale) / height;
    for (int cap = 0; cap <= 1; ++cap) // 0 = bottom ring, 1 = top ring
    {
        const float y = cap ? height : 0.f;
        const float r = cap ? radius * topScale : radius;
        for (int s = 0; s <= slices; ++s)
        {
            const float u = (float)s / (float)slices;
            const float theta = u * 2.f * kPi;
            const float cx = cosf(theta), cz = sinf(theta);
            MeshVertex mv;
            mv.position = Vec3(cx * r, y, cz * r);
            mv.normal = Vec3(cx, slope, cz).normalized();
            mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            mv.uv = Vec2(u, (float)cap);
            verts.push_back(mv);
        }
    }
    for (int s = 0; s < slices; ++s)
    {
        const u16 a = (u16)s, b = (u16)(s + slices + 1);
        const u16 quad[6] = {a, b, (u16)(a + 1), (u16)(a + 1), b, (u16)(b + 1)};
        idx.insert(idx.end(), quad, quad + 6);
    }
    // caps: center fans (bottom always; top only when it has area)
    for (int cap = 0; cap <= 1; ++cap)
    {
        const float r = cap ? radius * topScale : radius;
        if (r < 1e-5f) continue;
        const float y = cap ? height : 0.f;
        const float ny = cap ? 1.f : -1.f;
        const u16 center = (u16)verts.size();
        MeshVertex c;
        c.position = Vec3(0.f, y, 0.f);
        c.normal = Vec3(0.f, ny, 0.f);
        c.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
        c.uv = Vec2(0.5f, 0.5f);
        verts.push_back(c);
        for (int s = 0; s <= slices; ++s)
        {
            const float theta = (float)s / (float)slices * 2.f * kPi;
            MeshVertex mv;
            mv.position = Vec3(cosf(theta) * r, y, sinf(theta) * r);
            mv.normal = Vec3(0.f, ny, 0.f);
            mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            mv.uv = Vec2(cosf(theta) * 0.5f + 0.5f, sinf(theta) * 0.5f + 0.5f);
            verts.push_back(mv);
        }
        for (int s = 0; s < slices; ++s)
        {
            const u16 a = (u16)(center + 1 + s), b = (u16)(a + 1);
            if (cap)
                idx.insert(idx.end(), {center, b, a});
            else
                idx.insert(idx.end(), {center, a, b});
        }
    }
    finish(out, verts, idx);
}

void cylinder(Mesh& out, float radius, float height, int slices)
{
    tube(out, radius, height, slices, 1.f);
}

void cone(Mesh& out, float radius, float height, int slices)
{
    tube(out, radius, height, slices, 0.f);
}

// bottom hemisphere (pole..equator) + an explicit second equator ring at
// y=height (the straight cylindrical body between the two) + top hemisphere
// (equator..pole). UV v runs 0 (bottom pole) to 1 (top pole).
void capsule(Mesh& out, float radius, float height, int rings, int slices)
{
    std::vector<MeshVertex> verts;
    std::vector<u16> idx;

    const int halfRings = rings;              // rings per hemisphere
    const int totalRings = halfRings * 2 + 2; // + the explicit top-equator row
    const float vScale = 1.f / (float)(totalRings - 1);

    int row = 0;
    auto addRing = [&](float y, float ringRadius, float ny, float nRingScale)
    {
        const float v = (float)row * vScale;
        for (int s = 0; s <= slices; ++s)
        {
            const float u = (float)s / (float)slices;
            const float theta = u * 2.f * kPi;
            const float cx = cosf(theta), cz = sinf(theta);
            MeshVertex mv;
            mv.position = Vec3(cx * ringRadius, y, cz * ringRadius);
            mv.normal = Vec3(cx * nRingScale, ny, cz * nRingScale).normalized();
            mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            mv.uv = Vec2(u, v);
            verts.push_back(mv);
        }
        ++row;
    };

    // bottom hemisphere: pole (phi=pi, y=-radius) down to the equator (phi=pi/2, y=0)
    for (int r = 0; r <= halfRings; ++r)
    {
        const float phi = kPi * (1.f - 0.5f * (float)r / (float)halfRings);
        addRing(radius * cosf(phi), radius * sinf(phi), cosf(phi), sinf(phi));
    }
    // straight body: the same equator, lifted to y=height (radial normal)
    addRing(height, radius, 0.f, 1.f);
    // top hemisphere: equator (phi=pi/2) up to the pole (phi=0, y=height+radius)
    for (int r = 1; r <= halfRings; ++r)
    {
        const float phi = kPi * 0.5f * (1.f - (float)r / (float)halfRings);
        addRing(height + radius * cosf(phi), radius * sinf(phi), cosf(phi), sinf(phi));
    }

    for (int r = 0; r < totalRings - 1; ++r)
    {
        for (int s = 0; s < slices; ++s)
        {
            const u16 a = (u16)(r * (slices + 1) + s);
            const u16 b = (u16)(a + slices + 1);
            const u16 quad[6] = {a, (u16)(a + 1), b, (u16)(a + 1), (u16)(b + 1), b};
            idx.insert(idx.end(), quad, quad + 6);
        }
    }
    finish(out, verts, idx);
}

// ring in the XY plane, hole along Z: major angle theta sweeps the big
// circle around Z, minor angle phi sweeps the tube cross-section around
// the tangent at theta (spanned by the radial XY direction and Z itself).
void torus(Mesh& out, float majorRadius, float minorRadius, int majorSegments, int minorSegments)
{
    std::vector<MeshVertex> verts;
    std::vector<u16> idx;
    verts.reserve((size_t)(majorSegments + 1) * (minorSegments + 1));

    for (int i = 0; i <= majorSegments; ++i)
    {
        const float u = (float)i / (float)majorSegments;
        const float theta = u * 2.f * kPi;
        const Vec3 radial(cosf(theta), sinf(theta), 0.f);
        const Vec3 center = radial * majorRadius;
        for (int j = 0; j <= minorSegments; ++j)
        {
            const float v = (float)j / (float)minorSegments;
            const float phi = v * 2.f * kPi;
            const Vec3 n = radial * cosf(phi) + Vec3(0.f, 0.f, 1.f) * sinf(phi);
            MeshVertex mv;
            mv.position = center + n * minorRadius;
            mv.normal = n;
            mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            mv.uv = Vec2(u, v);
            verts.push_back(mv);
        }
    }
    for (int i = 0; i < majorSegments; ++i)
    {
        for (int j = 0; j < minorSegments; ++j)
        {
            const u16 a = (u16)(i * (minorSegments + 1) + j);
            const u16 b = (u16)(a + minorSegments + 1);
            const u16 quad[6] = {a, b, (u16)(a + 1), (u16)(a + 1), b, (u16)(b + 1)};
            idx.insert(idx.end(), quad, quad + 6);
        }
    }
    finish(out, verts, idx);
}

} // namespace primitives
