#include "scene/CSG.hpp"
#include "scene/AssetManager.hpp"
#include "scene/Mesh.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace
{
constexpr float kPlaneEps = 1e-5f;

Vec3 lerp3(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
Vec2 lerp2(const Vec2& a, const Vec2& b, float t) { return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
Vec3 mulPoint(const Mat4& m, const Vec3& p)
{
    Vec4 r = m * Vec4(p.x, p.y, p.z, 1.f);
    return Vec3(r.x, r.y, r.z);
}
Vec3 mulNormal(const Mat4& m, const Vec3& n) // upper 3x3 (fine for scale/rot/translate)
{
    Vec3 r(m.c[0][0] * n.x + m.c[1][0] * n.y + m.c[2][0] * n.z,
          m.c[0][1] * n.x + m.c[1][1] * n.y + m.c[2][1] * n.z,
          m.c[0][2] * n.x + m.c[1][2] * n.y + m.c[2][2] * n.z);
    float l2 = r.length_squared();
    return l2 > 1e-12f ? r * (1.f / sqrtf(l2)) : n;
}

struct CSGVertex
{
    Vec3 position{0, 0, 0};
    Vec3 normal{0, 1, 0};
    Vec2 uv{0, 0};
    CSGVertex interpolate(const CSGVertex& o, float t) const
    {
        CSGVertex r;
        r.position = lerp3(position, o.position, t);
        Vec3 mn = lerp3(normal, o.normal, t);
        float l2 = mn.length_squared();
        r.normal = l2 > 1e-12f ? mn * (1.f / sqrtf(l2)) : normal;
        r.uv = lerp2(uv, o.uv, t);
        return r;
    }
};

struct CSGPlane
{
    Vec3 normal{0, 1, 0};
    float w = 0.f;
    static CSGPlane fromPoints(const Vec3& a, const Vec3& b, const Vec3& c)
    {
        CSGPlane p;
        p.normal = Vec3::Cross(b - a, c - a).normalized();
        p.w = Vec3::Dot(p.normal, a);
        return p;
    }
    void flip()
    {
        normal = normal * -1.f;
        w = -w;
    }
    float signedDistance(const Vec3& pt) const { return Vec3::Dot(normal, pt) - w; }
};

struct CSGPolygon
{
    std::vector<CSGVertex> vertices;
    CSGPlane plane;
    int matIndex = 0;
    void flip()
    {
        std::reverse(vertices.begin(), vertices.end());
        for (auto& v : vertices) v.normal = v.normal * -1.f;
        plane.flip();
    }
    bool isDegenerate() const
    {
        if (vertices.size() < 3) return true;
        return Vec3::Cross(vertices[1].position - vertices[0].position,
                           vertices[2].position - vertices[0].position)
                  .length_squared() < 1e-12f;
    }
};

enum class Side
{
    Front,
    Back,
    Coplanar,
    Spanning
};
Side classifyPoint(const CSGPlane& pl, const Vec3& p)
{
    float d = pl.signedDistance(p);
    if (d > kPlaneEps) return Side::Front;
    if (d < -kPlaneEps) return Side::Back;
    return Side::Coplanar;
}
Side classifyPolygon(const CSGPlane& pl, const CSGPolygon& poly)
{
    int nf = 0, nb = 0;
    for (const auto& v : poly.vertices)
    {
        Side s = classifyPoint(pl, v.position);
        if (s == Side::Front) ++nf;
        else if (s == Side::Back) ++nb;
    }
    if (nf > 0 && nb > 0) return Side::Spanning;
    if (nf > 0) return Side::Front;
    if (nb > 0) return Side::Back;
    return Side::Coplanar;
}

void splitPolygon(const CSGPlane& plane, const CSGPolygon& polygon, std::vector<CSGPolygon>& coFront,
                  std::vector<CSGPolygon>& coBack, std::vector<CSGPolygon>& front,
                  std::vector<CSGPolygon>& back)
{
    switch (classifyPolygon(plane, polygon))
    {
    case Side::Coplanar:
        (Vec3::Dot(plane.normal, polygon.plane.normal) > 0.f ? coFront : coBack).push_back(polygon);
        break;
    case Side::Front: front.push_back(polygon); break;
    case Side::Back: back.push_back(polygon); break;
    case Side::Spanning:
    {
        std::vector<CSGVertex> fv, bv;
        size_t count = polygon.vertices.size();
        for (size_t i = 0; i < count; ++i)
        {
            size_t j = (i + 1) % count;
            const CSGVertex &vi = polygon.vertices[i], &vj = polygon.vertices[j];
            Side si = classifyPoint(plane, vi.position), sj = classifyPoint(plane, vj.position);
            if (si != Side::Back) fv.push_back(vi);
            if (si != Side::Front) bv.push_back(vi);
            if ((si == Side::Front && sj == Side::Back) || (si == Side::Back && sj == Side::Front))
            {
                float di = plane.signedDistance(vi.position), dj = plane.signedDistance(vj.position);
                CSGVertex mid = vi.interpolate(vj, Clamp(di / (di - dj), 0.f, 1.f));
                fv.push_back(mid);
                bv.push_back(mid);
            }
        }
        if (fv.size() >= 3)
        {
            CSGPolygon fp;
            fp.vertices = std::move(fv);
            fp.plane = polygon.plane;
            fp.matIndex = polygon.matIndex;
            if (!fp.isDegenerate()) front.push_back(std::move(fp));
        }
        if (bv.size() >= 3)
        {
            CSGPolygon bp;
            bp.vertices = std::move(bv);
            bp.plane = polygon.plane;
            bp.matIndex = polygon.matIndex;
            if (!bp.isDegenerate()) back.push_back(std::move(bp));
        }
        break;
    }
    }
}

struct BSPNode
{
    CSGPlane plane;
    std::vector<CSGPolygon> polygons;
    BSPNode* front = nullptr;
    BSPNode* back = nullptr;

    BSPNode() = default;
    explicit BSPNode(const std::vector<CSGPolygon>& p) { build(p); }
    ~BSPNode()
    {
        delete front;
        delete back;
    }
    BSPNode(const BSPNode&) = delete;
    BSPNode& operator=(const BSPNode&) = delete;

    void invert()
    {
        plane.flip();
        for (auto& p : polygons) p.flip();
        std::swap(front, back);
        if (front) front->invert();
        if (back) back->invert();
    }
    std::vector<CSGPolygon> allPolygons() const
    {
        std::vector<CSGPolygon> r = polygons;
        if (front)
        {
            auto f = front->allPolygons();
            r.insert(r.end(), f.begin(), f.end());
        }
        if (back)
        {
            auto b = back->allPolygons();
            r.insert(r.end(), b.begin(), b.end());
        }
        return r;
    }
    void clipTo(const BSPNode& other)
    {
        polygons = other.clipPolygons(polygons);
        if (front) front->clipTo(other);
        if (back) back->clipTo(other);
    }
    std::vector<CSGPolygon> clipPolygons(const std::vector<CSGPolygon>& polys) const
    {
        std::vector<CSGPolygon> fl, bl, cf, cb;
        for (const auto& p : polys) splitPolygon(plane, p, cf, cb, fl, bl);
        fl.insert(fl.end(), cf.begin(), cf.end());
        if (front) fl = front->clipPolygons(fl);
        if (back) bl = back->clipPolygons(bl);
        else bl.clear();
        fl.insert(fl.end(), bl.begin(), bl.end());
        return fl;
    }
    void build(const std::vector<CSGPolygon>& polys)
    {
        if (polys.empty()) return;
        plane = polys[0].plane;
        std::vector<CSGPolygon> fl, bl, cf, cb;
        for (const auto& p : polys) splitPolygon(plane, p, cf, cb, fl, bl);
        polygons.insert(polygons.end(), cf.begin(), cf.end());
        polygons.insert(polygons.end(), cb.begin(), cb.end());
        if (!fl.empty())
        {
            if (!front) front = new BSPNode();
            front->build(fl);
        }
        if (!bl.empty())
        {
            if (!back) back = new BSPNode();
            back->build(bl);
        }
    }
};

int maxSlot(const Mesh& m)
{
    int mx = 0;
    for (const Surface& s : m.surfaces()) mx = (s.material_slot > mx) ? s.material_slot : mx;
    return mx;
}

std::vector<CSGPolygon> meshToPolygons(const Mesh& mesh, const Mat4& transform, int matOffset)
{
    std::vector<CSGPolygon> polys;
    const std::vector<MeshVertex>& V = mesh.vertices();
    const std::vector<u32>& I = mesh.indices();
    auto findMat = [&](u32 triStart) -> int {
        for (const Surface& s : mesh.surfaces())
            if (triStart >= s.first_index && triStart < s.first_index + s.index_count) return s.material_slot + matOffset;
        return matOffset;
    };
    for (size_t i = 0; i + 2 < I.size(); i += 3)
    {
        const MeshVertex &a = V[I[i]], &b = V[I[i + 1]], &c = V[I[i + 2]];
        CSGVertex ca, cb, cc;
        ca.position = mulPoint(transform, a.position);
        ca.normal = mulNormal(transform, a.normal);
        ca.uv = a.uv;
        cb.position = mulPoint(transform, b.position);
        cb.normal = mulNormal(transform, b.normal);
        cb.uv = b.uv;
        cc.position = mulPoint(transform, c.position);
        cc.normal = mulNormal(transform, c.normal);
        cc.uv = c.uv;
        if (Vec3::Cross(cb.position - ca.position, cc.position - ca.position).length_squared() < 1e-12f) continue;
        CSGPolygon poly;
        poly.vertices = {ca, cb, cc};
        poly.plane = CSGPlane::fromPoints(ca.position, cb.position, cc.position);
        poly.matIndex = findMat((u32)i);
        polys.push_back(std::move(poly));
    }
    return polys;
}

Mesh* buildResultMesh(const char* name, const std::vector<CSGPolygon>& polygons, const CSG::Options& opts)
{
    if (polygons.empty()) return nullptr;
    assets::AssetManager& assetMgr = assets::AssetManager::instance();
    Mesh* existing = assetMgr.getMesh(name);
    if (existing) return existing;

    std::vector<MeshVertex> verts;
    std::vector<u32> indices;
    struct Surf
    {
        u32 start, count;
        int slot;
    };
    std::vector<Surf> surfs;

    std::unordered_map<int, std::vector<const CSGPolygon*>> byMat;
    std::vector<int> order;
    for (const auto& poly : polygons)
    {
        if (byMat.find(poly.matIndex) == byMat.end()) order.push_back(poly.matIndex);
        byMat[poly.matIndex].push_back(&poly);
    }

    for (int slot : order)
    {
        u32 start = (u32)indices.size();
        for (const CSGPolygon* poly : byMat[slot])
        {
            if (poly->vertices.size() < 3) continue;
            u32 base = (u32)verts.size();
            for (const auto& cv : poly->vertices)
            {
                MeshVertex v{};
                v.position = cv.position;
                v.normal = cv.normal;
                v.uv = cv.uv;
                v.tangent = Vec4(1, 0, 0, 1);
                verts.push_back(v);
            }
            for (u32 j = 1; j + 1 < (u32)poly->vertices.size(); ++j)
            {
                indices.push_back(base);
                indices.push_back(base + j);
                indices.push_back(base + j + 1);
            }
        }
        u32 count = (u32)indices.size() - start;
        if (count > 0)
        {
            Surf s;
            s.start = start;
            s.count = count;
            s.slot = slot;
            surfs.push_back(s);
        }
    }
    if (verts.empty()) return nullptr;

    Mesh* result = assetMgr.createMesh(name);
    result->set_data(verts.data(), (u32)verts.size(), indices.data(), (u32)indices.size());
    if (opts.smoothNormals) result->compute_normals();
    result->compute_tangents();
    // the 3-arg overload defaults Surface::bounds to a zero-size box at the
    // origin, which Scene::collect_instance culls against per-surface (see
    // MD3Loader.cpp's load_md3_mesh for the same trap) — the whole mesh's
    // bounds is a safe (if not tightest, for multi-surface results) box for
    // every surface, since each is a subset of all its vertices.
    for (const Surf& s : surfs) result->add_surface(s.start, s.count, s.slot, result->bounds());
    result->upload();
    return result;
}

Mesh* solveCSG(const char* name, CSG::Operation op, const Mesh& A, const Mesh& B, const Mat4& matA,
              const Mat4& matB, const CSG::Options& opts)
{
    int offB = maxSlot(A) + 1;
    auto polysA = meshToPolygons(A, matA, 0);
    auto polysB = meshToPolygons(B, matB, offB);
    if (polysA.empty() || polysB.empty()) return nullptr;
    BSPNode* a = new BSPNode(polysA);
    BSPNode* b = new BSPNode(polysB);
    switch (op)
    {
    case CSG::Operation::Union:
        a->clipTo(*b);
        b->clipTo(*a);
        b->invert();
        b->clipTo(*a);
        b->invert();
        a->build(b->allPolygons());
        break;
    case CSG::Operation::Difference:
        a->invert();
        a->clipTo(*b);
        b->clipTo(*a);
        b->invert();
        b->clipTo(*a);
        b->invert();
        a->build(b->allPolygons());
        a->invert();
        break;
    case CSG::Operation::Intersection:
        a->invert();
        b->clipTo(*a);
        b->invert();
        a->clipTo(*b);
        b->clipTo(*a);
        a->build(b->allPolygons());
        a->invert();
        break;
    }
    Mesh* result = buildResultMesh(name, a->allPolygons(), opts);
    delete a;
    delete b;
    return result;
}

void splitByPlane(const std::vector<CSGPolygon>& polys, const Vec3& n, float d,
                  std::vector<CSGPolygon>& outFront, std::vector<CSGPolygon>& outBack)
{
    CSGPlane sp;
    sp.normal = n.normalized();
    sp.w = d;
    std::vector<CSGPolygon> cf, cb;
    for (const auto& p : polys) splitPolygon(sp, p, cf, cb, outFront, outBack);
    outFront.insert(outFront.end(), cf.begin(), cf.end());
    outBack.insert(outBack.end(), cb.begin(), cb.end());
}

std::vector<CSGPolygon> shrinkPolygons(const std::vector<CSGPolygon>& polys, float amount)
{
    struct VI
    {
        Vec3 sum{0, 0, 0};
        int count = 0;
    };
    std::unordered_map<long long, VI> vn;
    auto key = [](const Vec3& p) -> long long {
        long long x = (long long)lroundf(p.x * 1e3f), y = (long long)lroundf(p.y * 1e3f),
                 z = (long long)lroundf(p.z * 1e3f);
        return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL);
    };
    for (const auto& poly : polys)
        for (const auto& v : poly.vertices)
        {
            auto& i = vn[key(v.position)];
            i.sum += poly.plane.normal;
            ++i.count;
        }
    std::vector<CSGPolygon> out;
    out.reserve(polys.size());
    for (const auto& poly : polys)
    {
        CSGPolygon np = poly;
        for (auto& v : np.vertices)
        {
            const auto& info = vn[key(v.position)];
            Vec3 s = info.sum * (1.f / info.count);
            float l2 = s.length_squared();
            if (l2 < 1e-12f) continue;
            v.position -= s * (1.f / sqrtf(l2)) * amount;
        }
        if (np.vertices.size() >= 3 &&
            Vec3::Cross(np.vertices[1].position - np.vertices[0].position, np.vertices[2].position - np.vertices[0].position)
                    .length_squared() > 1e-12f)
            np.plane = CSGPlane::fromPoints(np.vertices[0].position, np.vertices[1].position, np.vertices[2].position);
        if (!np.isDegenerate()) out.push_back(std::move(np));
    }
    return out;
}
} // namespace

namespace CSG
{
Mesh* makeUnion(const char* name, const Mesh& A, const Mesh& B, const Mat4& mA, const Mat4& mB, const Options& o)
{
    return solveCSG(name, Operation::Union, A, B, mA, mB, o);
}
Mesh* makeDifference(const char* name, const Mesh& A, const Mesh& B, const Mat4& mA, const Mat4& mB,
                     const Options& o)
{
    return solveCSG(name, Operation::Difference, A, B, mA, mB, o);
}
Mesh* makeIntersection(const char* name, const Mesh& A, const Mesh& B, const Mat4& mA, const Mat4& mB,
                       const Options& o)
{
    return solveCSG(name, Operation::Intersection, A, B, mA, mB, o);
}
Mesh* compute(const char* name, Operation op, const Mesh& A, const Mesh& B, const Mat4& mA, const Mat4& mB,
             const Options& o)
{
    return solveCSG(name, op, A, B, mA, mB, o);
}

Mesh* makeSymmetricDifference(const char* name, const Mesh& A, const Mesh& B, const Mat4& mA, const Mat4& mB,
                              const Options& o)
{
    int offB = maxSlot(A) + 1;
    auto pA = meshToPolygons(A, mA, 0), pB = meshToPolygons(B, mB, offB);
    if (pA.empty() || pB.empty()) return nullptr;
    BSPNode* a1 = new BSPNode(pA);
    BSPNode* b1 = new BSPNode(pB);
    a1->invert();
    a1->clipTo(*b1);
    b1->clipTo(*a1);
    b1->invert();
    b1->clipTo(*a1);
    b1->invert();
    a1->build(b1->allPolygons());
    a1->invert();
    auto diffAB = a1->allPolygons();
    delete a1;
    delete b1;

    BSPNode* a2 = new BSPNode(pA);
    BSPNode* b2 = new BSPNode(pB);
    b2->invert();
    b2->clipTo(*a2);
    a2->clipTo(*b2);
    a2->invert();
    a2->clipTo(*b2);
    a2->invert();
    b2->build(a2->allPolygons());
    b2->invert();
    auto diffBA = b2->allPolygons();
    delete a2;
    delete b2;

    diffAB.insert(diffAB.end(), diffBA.begin(), diffBA.end());
    return buildResultMesh(name, diffAB, o);
}

Mesh* makeInvert(const char* name, const Mesh& A, const Mat4& mA, const Options& o)
{
    auto polys = meshToPolygons(A, mA, 0);
    if (polys.empty()) return nullptr;
    for (auto& p : polys) p.flip();
    return buildResultMesh(name, polys, o);
}

SplitResult makeSplit(const char* frontName, const char* backName, const Mesh& A, const Vec3& n, float d,
                      const Mat4& mA, const Options& o)
{
    auto polys = meshToPolygons(A, mA, 0);
    if (polys.empty()) return {};
    std::vector<CSGPolygon> frontPolys, backPolys;
    splitByPlane(polys, n, d, frontPolys, backPolys);
    SplitResult r;
    if (!frontPolys.empty()) r.front = buildResultMesh(frontName, frontPolys, o);
    if (!backPolys.empty()) r.back = buildResultMesh(backName, backPolys, o);
    return r;
}

Mesh* makeHollow(const char* name, const Mesh& A, float thickness, const Mat4& mA, const Options& o)
{
    auto outerP = meshToPolygons(A, mA, 0);
    if (outerP.empty()) return nullptr;
    auto innerP = shrinkPolygons(outerP, thickness);
    if (innerP.empty()) return nullptr;
    BSPNode* outer = new BSPNode(outerP);
    BSPNode* inner = new BSPNode(innerP);
    outer->invert();
    outer->clipTo(*inner);
    inner->clipTo(*outer);
    inner->invert();
    inner->clipTo(*outer);
    inner->invert();
    outer->build(inner->allPolygons());
    outer->invert();
    Mesh* result = buildResultMesh(name, outer->allPolygons(), o);
    delete outer;
    delete inner;
    return result;
}
} // namespace CSG
