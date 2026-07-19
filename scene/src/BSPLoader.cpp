// BSPLoader.cpp — Quake 3 BSP (IBSP v46) mesh loader for the coregl scene layer.
 // Imports polygon / mesh faces directly and tessellates Bezier patches.
// Vertices are converted from Z-up to Y-up.  One Material per texture group.
 

#include "scene/Mesh.hpp"
#include "scene/Material.hpp"
#include "scene/AssetManager.hpp"
#include "scene/BspInstance.hpp"
#include "scene/Math.hpp"
#include "scene/Filesystem.hpp"
#include "coregl/gl_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

// ---- Quake 3 BSP constants --------------------------------------------------
constexpr int kQ3Ident = 1347633737; // "IBSP"
constexpr int kQ3Version = 46;       // Quake 3
constexpr int kNumLumps = 17;
constexpr int kLumpEntities = 0;
constexpr int kLumpTextures = 1;
constexpr int kLumpPlanes = 2;
constexpr int kLumpNodes = 3;
constexpr int kLumpLeafs = 4;
constexpr int kLumpLeafBrushes = 6;
constexpr int kLumpModels = 7;
constexpr int kLumpVertices = 10;
constexpr int kLumpMeshVerts = 11;
constexpr int kLumpBrushes = 8;
constexpr int kLumpBrushSides = 9;
constexpr int kLumpFaces = 13;

constexpr int kFacePolygon = 1;
constexpr int kFacePatch = 2;
constexpr int kFaceMesh = 3;
constexpr int kLumpLightmaps = 14;

constexpr int kTextureSize = 72; // sizeof(bsp texture entry): name[64] + flags(4) + contents(4)
constexpr int kVertexSize = 44;  // sizeof(bsp vertex)
constexpr int kFaceSize = 104;   // sizeof(bsp face)
constexpr int kPlaneSize = 16;   // vec3 normal + float dist
constexpr int kNodeSize = 36;    // int planeIdx + int children[2] + int mins[3] + int maxs[3]
constexpr int kLeafSize = 48;    // 12 ints (cluster,area,mins[3],maxs[3],firstFace,numFaces,firstLeafBrush,numLeafBrushes) — real Q3 dleaf_t; confirmed against tmp/apocalyx/glbsp.h's BSPLeaf
constexpr int kBrushSize = 12;   // int firstSide + int numSides + int textureIdx
constexpr int kBrushSideSize = 8; // int planeIdx + int textureIdx
constexpr int kModelSize = 40;   // vec3 mins + vec3 maxs + int firstFace + int numFaces + int firstBrush + int numBrushes

constexpr int kContentsSolid = 1;       // CONTENTS_SOLID
constexpr int kContentsPlayerClip = 0x10000; // CONTENTS_PLAYERCLIP — invisible,
// player-only collision brushes mappers lay over jagged stairs/ramp geometry
// so the PLAYER slides on a smooth clip hull instead of the visible steps.
// We only ever collide the player (no weapons/monsters here), so this is
// exactly as "solid" as CONTENTS_SOLID for our purposes. Missing this was a
// real bug: on bigger, properly-compiled Quake3 maps (Urban Terror's) that
// actually use clip brushes, the player caught on the raw visible stair
// edges underneath instead of the intended smooth ramp — small ledges and
// stairs "getting stuck" was this, not a math bug in the trace itself.
constexpr float kContactEps = 0.03125f; // Quake's own DIST_EPSILON convention (1/32 unit)

constexpr float kEps = 1e-6f;
constexpr int kPatchTess = 5; // Bezier subdivision per 2x2 block

// ---- Binary helpers ---------------------------------------------------------
struct Lump
{
    int offset;
    int length;
};

struct BspVertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec2 lmuv; // lightmap UV (vertex bytes 20..28)
};

struct Face
{
    int textureIdx;
    int faceType;
    int firstVert;
    int numVerts;
    int firstMeshVert;
    int numMeshVerts;
    int lm_index;    // lightmap page (-1 = none)
    int lm_start[2]; // pixel offset in lightmap page
    int lm_size[2];  // pixel size of lightmap region
    int patchW;
    int patchH;
};

int rdI32(const u8* data, int off)
{
    int v = 0;
    std::memcpy(&v, data + off, sizeof(int));
    return v;
}

float rdF32(const u8* data, int off)
{
    float v = 0.f;
    std::memcpy(&v, data + off, sizeof(float));
    return v;
}

std::string rdStr(const u8* data, u32 dataSize, int off)
{
    std::string s;
    s.reserve(32);
    while (off < (int)dataSize && data[off] != 0)
    {
        s.push_back(static_cast<char>(data[off]));
        ++off;
    }
    return s;
}

// Bounds-checked lump accessor.  Returns base byte pointer + element count.
const u8* lumpInfo(const u8* data, u32 dataSize, const Lump& l, int elemSize, int& count)
{
    if (l.offset < 0 || l.length < 0 || elemSize <= 0)
    {
        count = 0;
        return nullptr;
    }
    count = l.length / elemSize;
    if (l.offset + l.length > (int)dataSize)
    {
        count = 0;
        return nullptr;
    }
    return data + l.offset;
}

// Convert a BSP vertex into an engine MeshVertex (Y-up, normalised normal).
MeshVertex makeVertex(const BspVertex& bv)
{
    MeshVertex mv;
    mv.position = bv.position;
    Vec3 n = bv.normal;
    float lenSq = Vec3::Dot(n, n);
    mv.normal = (lenSq > kEps) ? n.normalized() : Vec3(0.f, 1.f, 0.f);
    mv.uv = bv.uv;
    mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
    return mv;
}

// ---- Bezier patch helpers (quadratic, 3x3 control net) ----------------------
// Evaluates position, normal, and uv from 9 BspVertex control points.
static void patchBasis(float t, float& b0, float& b1, float& b2)
{
    float it = 1.f - t; b0 = it * it; b1 = 2.f * t * it; b2 = t * t;
}

static float evalScalar(float p00, float p10, float p20, float p01, float p11, float p21,
                        float p02, float p12, float p22,
                        float u0, float u1, float u2, float v0, float v1, float v2)
{
    float r0 = p00 * u0 + p10 * u1 + p20 * u2;
    float r1 = p01 * u0 + p11 * u1 + p21 * u2;
    float r2 = p02 * u0 + p12 * u1 + p22 * u2;
    return r0 * v0 + r1 * v1 + r2 * v2;
}

static BspVertex evalPatch(const BspVertex cp[9], float u, float v)
{
    float u0, u1, u2, v0, v1, v2;
    patchBasis(u, u0, u1, u2);
    patchBasis(v, v0, v1, v2);
    auto S = [&](float a, float b, float c, float d, float e, float f,
                 float g, float h, float i) {
        return evalScalar(a, b, c, d, e, f, g, h, i, u0, u1, u2, v0, v1, v2);
    };
    BspVertex o{};
    o.position.x = S(cp[0].position.x,cp[1].position.x,cp[2].position.x,
                     cp[3].position.x,cp[4].position.x,cp[5].position.x,
                     cp[6].position.x,cp[7].position.x,cp[8].position.x);
    o.position.y = S(cp[0].position.y,cp[1].position.y,cp[2].position.y,
                     cp[3].position.y,cp[4].position.y,cp[5].position.y,
                     cp[6].position.y,cp[7].position.y,cp[8].position.y);
    o.position.z = S(cp[0].position.z,cp[1].position.z,cp[2].position.z,
                     cp[3].position.z,cp[4].position.z,cp[5].position.z,
                     cp[6].position.z,cp[7].position.z,cp[8].position.z);
    o.normal.x   = S(cp[0].normal.x,cp[1].normal.x,cp[2].normal.x,
                     cp[3].normal.x,cp[4].normal.x,cp[5].normal.x,
                     cp[6].normal.x,cp[7].normal.x,cp[8].normal.x);
    o.normal.y   = S(cp[0].normal.y,cp[1].normal.y,cp[2].normal.y,
                     cp[3].normal.y,cp[4].normal.y,cp[5].normal.y,
                     cp[6].normal.y,cp[7].normal.y,cp[8].normal.y);
    o.normal.z   = S(cp[0].normal.z,cp[1].normal.z,cp[2].normal.z,
                     cp[3].normal.z,cp[4].normal.z,cp[5].normal.z,
                     cp[6].normal.z,cp[7].normal.z,cp[8].normal.z);
    o.uv.x       = S(cp[0].uv.x,cp[1].uv.x,cp[2].uv.x,
                     cp[3].uv.x,cp[4].uv.x,cp[5].uv.x,
                     cp[6].uv.x,cp[7].uv.x,cp[8].uv.x);
    o.uv.y       = S(cp[0].uv.y,cp[1].uv.y,cp[2].uv.y,
                     cp[3].uv.y,cp[4].uv.y,cp[5].uv.y,
                     cp[6].uv.y,cp[7].uv.y,cp[8].uv.y);
    o.lmuv.x     = S(cp[0].lmuv.x,cp[1].lmuv.x,cp[2].lmuv.x,
                     cp[3].lmuv.x,cp[4].lmuv.x,cp[5].lmuv.x,
                     cp[6].lmuv.x,cp[7].lmuv.x,cp[8].lmuv.x);
    o.lmuv.y     = S(cp[0].lmuv.y,cp[1].lmuv.y,cp[2].lmuv.y,
                     cp[3].lmuv.y,cp[4].lmuv.y,cp[5].lmuv.y,
                     cp[6].lmuv.y,cp[7].lmuv.y,cp[8].lmuv.y);
    return o;
}

// Try loading a texture from `textureDir` + `name` with several extensions.
// Q3 BSP texture names are like "textures/base_floor/clanggrate"; we join
// directly with textureDir (the map root, e.g. "assets/bsp/oa_rpg3dm2/").
// AssetManager never returns null (checkerboard fallback).
gl::Texture* tryLoadTex(const std::string& textureDir, const std::string& name)
{
    const char* exts[] = {".tga", ".jpg", ".png", ".bmp"};

    for (const char* e : exts)
    {
        std::string fullPath = textureDir + name + e;
        if (fs::getFilesystem().exists(fullPath.c_str()))
            return assets::AssetManager::instance().loadTexture(name.c_str(), fullPath.c_str(), true);
    }
    // Fallback — AssetManager returns checkerboard so the scene keeps rendering.
    return assets::AssetManager::instance().loadTexture(name.c_str(),
                                                        (textureDir + name + ".tga").c_str(),
                                                        true);
}

// ---- entities lump: "{ \"key\" \"value\" ... }" blocks, no factory yet ------
std::vector<BspInstance::Entity> parseEntities(const std::string& text)
{
    std::vector<BspInstance::Entity> ents;
    size_t i = 0;
    while (i < text.size())
    {
        size_t open = text.find('{', i);
        if (open == std::string::npos) break;
        size_t close = text.find('}', open);
        if (close == std::string::npos) break;

        BspInstance::Entity e;
        size_t p = open + 1;
        while (p < close)
        {
            size_t k0 = text.find('"', p);
            if (k0 == std::string::npos || k0 >= close) break;
            size_t k1 = text.find('"', k0 + 1);
            if (k1 == std::string::npos || k1 >= close) break;
            size_t v0 = text.find('"', k1 + 1);
            if (v0 == std::string::npos || v0 >= close) break;
            size_t v1 = text.find('"', v0 + 1);
            if (v1 == std::string::npos || v1 >= close) break;

            std::string key = text.substr(k0 + 1, k1 - k0 - 1);
            std::string value = text.substr(v0 + 1, v1 - v0 - 1);
            e.keyValues[key] = value;
            p = v1 + 1;
        }
        auto it = e.keyValues.find("classname");
        e.classname = (it != e.keyValues.end()) ? it->second : "";
        gl::Log::Info("[BSP] entity: classname='%s' keys=%zu", e.classname.c_str(), e.keyValues.size());
        ents.push_back(std::move(e));
        i = close + 1;
    }
    return ents;
}

// Q3 "origin" values are "x y z" in Z-up space; swap to engine Y-up, same
// convention as makeVertex()'s Z-up -> Y-up swap above.
bool parseOriginYUp(const std::string& s, Vec3& out)
{
    float x, y, z;
    if (std::sscanf(s.c_str(), "%f %f %f", &x, &y, &z) != 3) return false;
    out = Vec3(x, z, y);
    return true;
}

} // namespace

// ============================================================================
// BspInstance collision: recursive box trace through the BSP tree (Quake's
// classic CM_RecursiveHullCheck shape) + leaf/brush clip (ClipBoxToBrush).
// Own implementation against our own Vec3/types — not a port of any single
// reference; the technique is the standard one shared by apocalyx's GLBsp,
// 6dx's collider.cpp and Genesis3D's Trace.c (see the BSP plan for details).
// ============================================================================

bool BspInstance::clipBoxToBrush(const BspBrush& brush, const Vec3& origStart, const Vec3& origEnd,
                                 const Vec3& halfExtent, BspTraceResult& result) const
{
    // Always tested against the trace's ORIGINAL start/end — never a
    // node-split sub-segment — so `enterFrac` below is already in the
    // trace's own 0..1 scale, no remap needed. See the long comment on the
    // declaration in BspInstance.hpp for why that matters.
    float enterFrac = -1.f;
    float leaveFrac = 1.f;
    bool startsOut = false;
    bool getsOut = false;
    const BspPlane* clipPlane = nullptr;

    for (int i = 0; i < brush.numSides; ++i)
    {
        const BspBrushSide& side = m_bspBrushSides[brush.firstSide + i];
        const BspPlane& plane = m_bspPlanes[side.planeIndex];

        float offset = std::fabs(halfExtent.x * plane.normal.x) +
                       std::fabs(halfExtent.y * plane.normal.y) +
                       std::fabs(halfExtent.z * plane.normal.z);
        float dist = plane.dist + offset;

        float d1 = Vec3::Dot(plane.normal, origStart) - dist;
        float d2 = Vec3::Dot(plane.normal, origEnd) - dist;

        if (d2 > 0.f) getsOut = true;
        if (d1 > 0.f) startsOut = true;

        if (d1 > 0.f && d2 > 0.f) return false; // fully outside this plane, never enters the brush
        if (d1 <= 0.f && d2 <= 0.f) continue;   // fully behind this plane, doesn't clip the sweep

        // kContactEps shrinks the stop point slightly short of the true
        // geometric surface (entering) / slightly past it (leaving) — so a
        // resolved position is NEVER left exactly on a plane, where the
        // next frame's d1 would be an ambiguous ~0 and misclassify as
        // already-embedded. Same technique as apocalyx's GLBsp and
        // DigiBen's BSP Loader Part 6 (both use kEpsilon/DIST_EPSILON =
        // 0.03125f this exact way), not a bandaid on the result afterward.
        if (d1 > d2) // entering through this plane
        {
            float f = (d1 - kContactEps) / (d1 - d2);
            if (f > enterFrac) { enterFrac = f; clipPlane = &plane; }
        }
        else // leaving through this plane
        {
            float f = (d1 + kContactEps) / (d1 - d2);
            if (f < leaveFrac) leaveFrac = f;
        }
    }

    if (!startsOut)
    {
        // start point already inside the brush's solid volume
        result.startSolid = true;
        if (!getsOut && result.fraction > 0.f)
        {
            result.hit = true;
            result.fraction = 0.f;
        }
        return true;
    }

    if (enterFrac < leaveFrac && enterFrac > -1.f && enterFrac < result.fraction)
    {
        if (enterFrac < 0.f) enterFrac = 0.f;
        result.fraction = enterFrac;
        result.normal = clipPlane ? clipPlane->normal : Vec3(0.f, 1.f, 0.f);
        result.hit = true;
        return true;
    }
    return false;
}

void BspInstance::traceLeaf(int leaf, const Vec3& origStart, const Vec3& origEnd,
                            const Vec3& halfExtent, BspTraceResult& result) const
{
    const BspLeaf& lf = m_bspLeafs[leaf];
    for (int i = 0; i < lf.numLeafBrushes; ++i)
    {
        int brushIdx = m_bspLeafBrushes[lf.firstLeafBrush + i];
        const BspBrush& brush = m_bspBrushes[brushIdx];
        if (!brush.solid) continue;
        clipBoxToBrush(brush, origStart, origEnd, halfExtent, result);
    }
}

void BspInstance::traceNode(int node, float startFrac, float endFrac, const Vec3& start,
                            const Vec3& end, const Vec3& origStart, const Vec3& origEnd,
                            const Vec3& halfExtent, BspTraceResult& result) const
{
    if (result.fraction <= startFrac) return; // a closer hit was already found elsewhere

    if (node < 0)
    {
        traceLeaf(~node, origStart, origEnd, halfExtent, result);
        return;
    }

    const BspNode& n = m_bspNodes[node];
    const BspPlane& plane = m_bspPlanes[n.planeIndex];

    float offset = std::fabs(halfExtent.x * plane.normal.x) +
                   std::fabs(halfExtent.y * plane.normal.y) +
                   std::fabs(halfExtent.z * plane.normal.z);

    float t1 = Vec3::Dot(plane.normal, start) - plane.dist;
    float t2 = Vec3::Dot(plane.normal, end) - plane.dist;

    if (t1 >= offset && t2 >= offset)
    {
        traceNode(n.children[0], startFrac, endFrac, start, end, origStart, origEnd, halfExtent, result);
        return;
    }
    if (t1 < -offset && t2 < -offset)
    {
        traceNode(n.children[1], startFrac, endFrac, start, end, origStart, origEnd, halfExtent, result);
        return;
    }

    // the sweep straddles this plane: split at the crossing point(s) and
    // recurse the near side first, then the far side. kContactEps here
    // matches clipBoxToBrush's — same reasoning, see its comment.
    int side;
    float frac1, frac2;
    if (t1 < t2)
    {
        float idist = 1.f / (t1 - t2);
        side = 1; // back first
        frac1 = (t1 - offset - kContactEps) * idist;
        frac2 = (t1 + offset + kContactEps) * idist;
    }
    else if (t1 > t2)
    {
        float idist = 1.f / (t1 - t2);
        side = 0; // front first
        frac1 = (t1 + offset + kContactEps) * idist;
        frac2 = (t1 - offset - kContactEps) * idist;
    }
    else
    {
        side = 0;
        frac1 = 0.f;
        frac2 = 1.f;
    }
    frac1 = frac1 < 0.f ? 0.f : (frac1 > 1.f ? 1.f : frac1);
    frac2 = frac2 < 0.f ? 0.f : (frac2 > 1.f ? 1.f : frac2);

    // near side: from `start` up to the first crossing point
    float midFrac1 = startFrac + (endFrac - startFrac) * frac1;
    Vec3 mid1 = start + (end - start) * frac1;
    traceNode(n.children[side], startFrac, midFrac1, start, mid1, origStart, origEnd, halfExtent, result);

    // far side: from the second crossing point onward to `end` — NOT
    // `start` again (a real bug here: reusing `start` corrupted every
    // traversal that crossed more than one splitting plane, which a short
    // vertical drop rarely does but any long horizontal sweep always does —
    // exactly why floor collision looked fine while wall collision found
    // nothing at all).
    float midFrac2 = startFrac + (endFrac - startFrac) * frac2;
    Vec3 mid2 = start + (end - start) * frac2;
    traceNode(n.children[side ^ 1], midFrac2, endFrac, mid2, end, origStart, origEnd, halfExtent, result);
}

BspTraceResult BspInstance::traceBox(const Vec3& start, const Vec3& end, const Vec3& halfExtent) const
{
    BspTraceResult result;
    result.fraction = 1.f;
    result.endPos = end;
    if (m_bspNodes.empty())
    {
        return result;
    }
    traceNode(m_bspRootNode, 0.f, 1.f, start, end, start, end, halfExtent, result);
    result.endPos = start + (end - start) * result.fraction;
    return result;
}


static bool isWalkableGround(const Vec3& n)
{
    return n.y >= 0.7f;
}
Vec3 BspInstance::tryStepUp(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
    bool* outStepped, bool* outGrounded) const
{
    if (outStepped) *outStepped = false;
    if (outGrounded) *outGrounded = false;

 
    constexpr float kStepHeight = 24.f;

    const Vec3 move = end - start;
    const Vec3 horizMove(move.x, 0.f, move.z);

    if (horizMove.length_squared() <= 1e-8f)
        return start;

    // Probe with a narrower horizontal footprint than the real body for the
    // up/forward/down search (full height kept — headroom still needs to be
    // real). A full-width box can straddle two risers at once on stairs
    // whose tread depth is shorter than the player's own width, wedging it
    // between steps instead of climbing (this was a real bug here — the
    // player got stuck partway up a narrower staircase, camera ending up
    // jammed into the geometry). Same technique most engines use: probe
    // narrow, then verify the full body actually fits at the result before
    // committing to it.
    const Vec3 probeExtent(halfExtent.x * 0.5f, halfExtent.y, halfExtent.z * 0.5f);

    // PASSO 1: Subir (Trace UP)
    // Em vez de teleportar, fazemos sweep para cima. Se batermos num teto baixo,
    // o traceUp.endPos para antes de atravessar geometria sólida.
    Vec3 upTarget = start + Vec3(0.f, kStepHeight, 0.f);
    BspTraceResult traceUp = traceBox(start, upTarget, probeExtent);

    Vec3 steppedStart = traceUp.endPos;

    // PASSO 2: Avançar (Trace FORWARD)
    // Tenta mover para a frente a partir do ponto mais alto que conseguimos subir.
    Vec3 forwardTarget = steppedStart + horizMove;
    BspTraceResult traceForward = traceBox(steppedStart, forwardTarget, probeExtent);

    // Crítico: Se o sweep horizontal mal avançou (bateu numa parede logo a seguir ao degrau),
    // consideramos o step um fracasso para evitar que o jogador fique preso em esquinas.
    if (traceForward.fraction < 0.01f)
        return start;

    Vec3 forwardEnd = traceForward.endPos;

    // PASSO 3: Descer (Trace DOWN)
    // Descemos a mesma altura que tentámos subir originalmente, mais uma margem (2.0f)
    // para garantir que a colisão com o chão regista corretamente.
    Vec3 downTarget = forwardEnd - Vec3(0.f, kStepHeight + 2.0f, 0.f);
    BspTraceResult traceDown = traceBox(forwardEnd, downTarget, probeExtent);

    // Se o sweep para baixo não bateu em nada, não é um degrau, é um buraco/precipício.
    if (!traceDown.hit)
        return start;

    // Se aterra num declive acentuado que não é "camiho", rejeita.
    if (!isWalkableGround(traceDown.normal))
        return start;

    // Verification: does the REAL, full-width body actually fit at the
    // narrow probe's result? A zero-length trace at that spot with the
    // real halfExtent just checks for embedding (startSolid) — if the
    // narrow probe found a landing too tight for the real body, reject
    // rather than wedge the player into geometry.
    BspTraceResult fitCheck = traceBox(traceDown.endPos, traceDown.endPos, halfExtent);
    if (fitCheck.startSolid)
        return start;

    // O Step foi bem sucedido.
    if (outStepped) *outStepped = true;
    if (outGrounded) *outGrounded = true;

    return traceDown.endPos;
}

Vec3 BspInstance::moveAndSlide(const Vec3& start, const Vec3& end,
    const Vec3& halfExtent, BspCollisionResponse response, bool* outGrounded) const
{
    Vec3 pos = start;
    Vec3 remaining = end - start;
    bool grounded = false;

    for (int pass = 0; pass < 4; ++pass)
    {
        if (remaining.length_squared() <= 1e-8f)
            break;

        Vec3 target = pos + remaining;
        BspTraceResult tr = traceBox(pos, target, halfExtent);

        Vec3 reached = tr.endPos;
        Vec3 leftover = target - reached;
        pos = reached;

        if (!tr.hit)
            break;

        if (isWalkableGround(tr.normal))
            grounded = true;

        if (response == BspCollisionResponse::Stop)
            break;

        // Qualquer superfície íngreme demais para andar é tratada como um potencial degrau
        const bool lateralWall = std::fabs(tr.normal.y) < 0.7f;
        const bool hasHorizontalIntent =
            (remaining.x * remaining.x + remaining.z * remaining.z) > 1e-8f;

        // the ordinary slide result for this pass — computed regardless of
        // whether we also try stepping, so the two can be compared
        float d = leftover.dot(tr.normal);
        Vec3 slid = leftover - tr.normal * d;
        if (response == BspCollisionResponse::SlideXZ)
            slid.y = 0.f;

        if (lateralWall && hasHorizontalIntent)
        {
            bool stepped = false;
            bool stepGrounded = false;
            Vec3 stepPos = tryStepUp(pos, target, halfExtent, &stepped, &stepGrounded);

            // Only take the step if it actually makes at least as much
            // horizontal progress as sliding along the wall would have —
            // this is what makes the LAST step of a staircase behave: once
            // you're on the top landing, sliding already goes straight
            // through (no riser left to climb), so it naturally wins over
            // stepping instead of tryStepUp needing to special-case "is
            // this the top." Same technique real engines use (Source's
            // StepMove / Quake3's PM_StepSlideMove) — compare, don't guess.
            if (stepped)
            {
                Vec3 stepHoriz(stepPos.x - pos.x, 0.f, stepPos.z - pos.z);
                Vec3 slideHoriz(slid.x, 0.f, slid.z);
                if (stepHoriz.length_squared() >= slideHoriz.length_squared() * 0.9f)
                {
                    pos = stepPos;
                    grounded = grounded || stepGrounded;

                    // Manter apenas a direção horizontal que faltava percorrer.
                    // Se não cortarmos o Y (0.f), o boneco afunda no chão.
                    Vec3 unMoved = target - pos;
                    remaining = Vec3(unMoved.x, 0.f, unMoved.z);
                    continue;
                }
            }
        }

        remaining = slid;
    }

    // Extra ground probe, independent of whatever happened during the move
    // above: a short trace straight down from the resolved position. Purely
    // relying on "was the last hit this pass a floor normal" misses cases
    // where a wide box is resting on a narrow stair tread — gravity's own
    // downward trace can land on a step's edge/corner or an already-settled
    // contact that never re-triggers a fresh hit this frame, so `grounded`
    // stays false even though the player is clearly standing on something
    // (symptom: can't jump while standing still on stairs). Same idea as a
    // CharacterController's separate "IsGrounded" foot-probe in Unity/Unreal
    // — decoupled from the movement trace entirely.
    if (!grounded)
    {
        BspTraceResult groundProbe = traceBox(pos, pos - Vec3(0.f, 4.f, 0.f), halfExtent);
        if (groundProbe.hit && isWalkableGround(groundProbe.normal))
            grounded = true;
    }

    if (outGrounded)
        *outGrounded = grounded;

    return pos;
}

BspRayHit BspInstance::raycast(const Vec3& origin, const Vec3& direction, float maxDistance) const
{
    BspRayHit out;
    float len = direction.length();
    if (len < 1e-6f) return out;

    Vec3 dir = direction * (1.f / len);
    Vec3 end = origin + dir * maxDistance;
    BspTraceResult tr = traceBox(origin, end, Vec3(0.f, 0.f, 0.f));

    out.hit = tr.hit;
    out.point = tr.endPos;
    out.normal = tr.normal;
    out.distance = tr.fraction * maxDistance;
    return out;
}
// Vec3 BspInstance::moveAndSlide(const Vec3& start, const Vec3& end, const Vec3& halfExtent,
//                                BspCollisionResponse response, bool* outGrounded) const
// {
//     Vec3 pos = start;
//     Vec3 remaining = end - start;
//     bool grounded = false;

//     for (int pass = 0; pass < 4; ++pass)
//     {
//         if (remaining.length_squared() < 1e-8f) break;

//         Vec3 target = pos + remaining;
//         BspTraceResult tr = traceBox(pos, target, halfExtent);
//         Vec3 reached = tr.endPos;
//         Vec3 leftover = target - reached;
//         pos = reached;

//         if (!tr.hit) break;
//         if (tr.normal.y > 0.7f) grounded = true;

//         if (response == BspCollisionResponse::Stop) break;

//         float d = Vec3::Dot(leftover, tr.normal);
//         Vec3 slid = leftover - tr.normal * d;
//         if (response == BspCollisionResponse::SlideXZ) slid.y = 0.f;
//         remaining = slid;
//     }

//     if (outGrounded) *outGrounded = grounded;
//     return pos;
// }

bool BspInstance::find_spawn_point(Vec3& outPos) const
{
    const Entity* fallback = nullptr;
    for (const Entity& e : m_entities)
    {
        if (e.classname == "info_player_start")
        {
            auto it = e.keyValues.find("origin");
            if (it != e.keyValues.end() && parseOriginYUp(it->second, outPos)) return true;
        }
        else if (e.classname == "info_player_deathmatch" && !fallback)
        {
            fallback = &e;
        }
    }
    if (fallback)
    {
        auto it = fallback->keyValues.find("origin");
        if (it != fallback->keyValues.end() && parseOriginYUp(it->second, outPos)) return true;
    }
    return false;
}

bool BspInstance::entity_origin(const Entity& e, Vec3& outPos)
{
    auto it = e.keyValues.find("origin");
    if (it == e.keyValues.end()) return false;
    return parseOriginYUp(it->second, outPos);
}

bool BspInstance::entity_model_bounds(const Entity& e, Vec3& outMin, Vec3& outMax) const
{
    auto it = e.keyValues.find("model");
    if (it == e.keyValues.end() || it->second.empty() || it->second[0] != '*') return false;

    int idx = std::atoi(it->second.c_str() + 1);
    if (idx <= 0 || idx >= (int)m_bspModels.size()) return false; // model 0 is worldspawn, never referenced

    const BspModel& m = m_bspModels[idx];
    outMin = m.mins;
    outMax = m.maxs;
    return true;
}

int BspInstance::remove_entities(const std::string& classname)
{
    size_t before = m_entities.size();
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
                                    [&](const Entity& e) { return e.classname == classname; }),
                     m_entities.end());
    return static_cast<int>(before - m_entities.size());
}

bool assets::AssetManager::load_bsp_collision(BspInstance* inst, const char* path)
{
    if (!inst || !path) return false;

    scene::ByteArray bytes;
    if (!fs::getFilesystem().readFile(path, bytes))
    {
        gl::Log::Error("[BSP] cannot read '%s'", path);
        return false;
    }
    const u8* data = bytes.data();
    const u32 dataSize = bytes.size();

    if (dataSize < 8 + kNumLumps * 8)
    {
        gl::Log::Error("[BSP] truncated header '%s'", path);
        return false;
    }
    if (rdI32(data, 0) != kQ3Ident || rdI32(data, 4) != kQ3Version)
    {
        gl::Log::Error("[BSP] bad magic / version in '%s'", path);
        return false;
    }

    inst->loadCollisionFromBytes(data, dataSize, path);
    return true;
}

void BspInstance::loadCollisionFromBytes(const u8* data, u32 dataSize, const char* path)
{
    BspInstance* inst = this; // keep the body below identical to before the refactor

    std::vector<Lump> lumps(kNumLumps);
    for (int i = 0; i < kNumLumps; ++i)
    {
        lumps[i].offset = rdI32(data, 8 + i * 8);
        lumps[i].length = rdI32(data, 8 + i * 8 + 4);
    }

    // ---- textures: need "contents" (offset 68) to know which brushes are solid
    int numTextures = 0;
    const u8* texBase = lumpInfo(data, dataSize, lumps[kLumpTextures], kTextureSize, numTextures);
    std::vector<int> textureContents;
    textureContents.reserve(numTextures);
    for (int i = 0; i < numTextures; ++i)
        textureContents.push_back(rdI32(data, static_cast<int>((texBase - data) + i * kTextureSize + 68)));

    // ---- planes ---------------------------------------------------------
    int numPlanes = 0;
    const u8* planeBase = lumpInfo(data, dataSize, lumps[kLumpPlanes], kPlaneSize, numPlanes);
    inst->m_bspPlanes.clear();
    inst->m_bspPlanes.reserve(numPlanes);
    for (int i = 0; i < numPlanes; ++i)
    {
        int o = static_cast<int>((planeBase - data) + i * kPlaneSize);
        // Z-up -> Y-up, same swap as vertices: (x, z, y).
        BspInstance::BspPlane p;
        p.normal = Vec3(rdF32(data, o), rdF32(data, o + 8), rdF32(data, o + 4));
        p.dist = rdF32(data, o + 12);
        inst->m_bspPlanes.push_back(p);
    }

    // ---- nodes ------------------------------------------------------------
    int numNodes = 0;
    const u8* nodeBase = lumpInfo(data, dataSize, lumps[kLumpNodes], kNodeSize, numNodes);
    inst->m_bspNodes.clear();
    inst->m_bspNodes.reserve(numNodes);
    for (int i = 0; i < numNodes; ++i)
    {
        int o = static_cast<int>((nodeBase - data) + i * kNodeSize);
        BspInstance::BspNode n;
        n.planeIndex = rdI32(data, o);
        n.children[0] = rdI32(data, o + 4);
        n.children[1] = rdI32(data, o + 8);
        inst->m_bspNodes.push_back(n);
    }
    inst->m_bspRootNode = 0; // Q3's world tree always roots at node 0

    // ---- leafs --------------------------------------------------------------
    int numLeafs = 0;
    const u8* leafBase = lumpInfo(data, dataSize, lumps[kLumpLeafs], kLeafSize, numLeafs);
    inst->m_bspLeafs.clear();
    inst->m_bspLeafs.reserve(numLeafs);
    for (int i = 0; i < numLeafs; ++i)
    {
        int o = static_cast<int>((leafBase - data) + i * kLeafSize);
        BspInstance::BspLeaf lf;
        // 12 ints: cluster,area,mins[3],maxs[3],firstface,numfaces,firstbrush,numbrushes
        lf.firstLeafBrush = rdI32(data, o + 40);
        lf.numLeafBrushes = rdI32(data, o + 44);
        inst->m_bspLeafs.push_back(lf);
    }

    // ---- leafbrushes --------------------------------------------------------
    int numLeafBrushes = 0;
    const u8* lbBase = lumpInfo(data, dataSize, lumps[kLumpLeafBrushes], 4, numLeafBrushes);
    inst->m_bspLeafBrushes.clear();
    inst->m_bspLeafBrushes.reserve(numLeafBrushes);
    for (int i = 0; i < numLeafBrushes; ++i)
        inst->m_bspLeafBrushes.push_back(rdI32(data, static_cast<int>((lbBase - data) + i * 4)));

    // ---- brushsides -----------------------------------------------------
    int numBrushSides = 0;
    const u8* bsBase = lumpInfo(data, dataSize, lumps[kLumpBrushSides], kBrushSideSize, numBrushSides);
    inst->m_bspBrushSides.clear();
    inst->m_bspBrushSides.reserve(numBrushSides);
    for (int i = 0; i < numBrushSides; ++i)
    {
        int o = static_cast<int>((bsBase - data) + i * kBrushSideSize);
        BspInstance::BspBrushSide s;
        s.planeIndex = rdI32(data, o);
        inst->m_bspBrushSides.push_back(s);
    }

    // ---- brushes --------------------------------------------------------
    int numBrushes = 0;
    const u8* brushBase = lumpInfo(data, dataSize, lumps[kLumpBrushes], kBrushSize, numBrushes);
    inst->m_bspBrushes.clear();
    inst->m_bspBrushes.reserve(numBrushes);
    for (int i = 0; i < numBrushes; ++i)
    {
        int o = static_cast<int>((brushBase - data) + i * kBrushSize);
        BspInstance::BspBrush b;
        b.firstSide = rdI32(data, o);
        b.numSides = rdI32(data, o + 4);
        int texIdx = rdI32(data, o + 8);
        int contents = (texIdx >= 0 && texIdx < (int)textureContents.size()) ? textureContents[texIdx] : 0;
        b.solid = (contents & (kContentsSolid | kContentsPlayerClip)) != 0;
        inst->m_bspBrushes.push_back(b);
    }

    // ---- models (brush entities' own geometry — model 0 is worldspawn) ---
    int numModels = 0;
    const u8* modelBase = lumpInfo(data, dataSize, lumps[kLumpModels], kModelSize, numModels);
    inst->m_bspModels.clear();
    inst->m_bspModels.reserve(numModels);
    for (int i = 0; i < numModels; ++i)
    {
        int o = static_cast<int>((modelBase - data) + i * kModelSize);
        BspInstance::BspModel md;
        // Z-up -> Y-up, same (x, z, y) swap as everything else in this file.
        md.mins = Vec3(rdF32(data, o), rdF32(data, o + 8), rdF32(data, o + 4));
        md.maxs = Vec3(rdF32(data, o + 12), rdF32(data, o + 20), rdF32(data, o + 16));
        md.firstBrush = rdI32(data, o + 32);
        md.numBrushes = rdI32(data, o + 36);
        inst->m_bspModels.push_back(md);
    }

    // ---- entities (raw text) ---------------------------------------------
    const Lump& entLump = lumps[kLumpEntities];
    std::string entText;
    if (entLump.offset >= 0 && entLump.length > 0 &&
        entLump.offset + entLump.length <= (int)dataSize)
    {
        entText.assign(reinterpret_cast<const char*>(data + entLump.offset), entLump.length);
    }
    inst->m_entities = parseEntities(entText);

    // classname breakdown — so it's actually clear what got loaded, not
    // just a count (89 entities tells you nothing about what they are)
    std::unordered_map<std::string, int> classCounts;
    for (const BspInstance::Entity& e : inst->m_entities) ++classCounts[e.classname];
    std::string breakdown;
    for (const auto& kv : classCounts)
    {
        if (!breakdown.empty()) breakdown += ", ";
        breakdown += kv.first + " x" + std::to_string(kv.second);
    }

    gl::Log::Info("[BSP] '%s' collision: planes=%d nodes=%d leafs=%d brushes=%d models=%d entities=%d",
                  path, numPlanes, numNodes, numLeafs, numBrushes, numModels,
                  (int)inst->m_entities.size());
    gl::Log::Info("[BSP] '%s' entity classes: %s", path, breakdown.c_str());
}

Mesh* assets::AssetManager::load_bsp_mesh(const char* name, const char* path,
                     std::vector<Material*>& out_mats,
                     const char* textureDir,
                     BspInstance* collision)
{
    if (!name || !path) return nullptr;

    // Already loaded? (rare path: a second BspInstance for the same map —
    // still needs its own collision fill, so this falls back to a real
    // re-read rather than skip it silently)
    Mesh* existing = getMesh(name);
    if (existing)
    {
        if (collision) load_bsp_collision(collision, path);
        return existing;
    }

    // ---- read the whole file into memory ------------------------------------
    scene::ByteArray bytes;
    if (!fs::getFilesystem().readFile(path, bytes))
    {
        gl::Log::Error("[BSP] cannot read '%s'", path);
        return nullptr;
    }
    const u8* data = bytes.data();
    const u32 dataSize = bytes.size();

    if (dataSize < 8 + kNumLumps * 8)
    {
        gl::Log::Error("[BSP] truncated header '%s'", path);
        return nullptr;
    }
    if (rdI32(data, 0) != kQ3Ident || rdI32(data, 4) != kQ3Version)
    {
        gl::Log::Error("[BSP] bad magic / version in '%s'", path);
        return nullptr;
    }

    // Collision + entities, straight off this same in-memory buffer — no
    // second file read (see BspInstance::loadCollisionFromBytes's comment).
    if (collision) collision->loadCollisionFromBytes(data, dataSize, path);

    // ---- parse lump table ---------------------------------------------------
    std::vector<Lump> lumps(kNumLumps);
    for (int i = 0; i < kNumLumps; ++i)
    {
        lumps[i].offset = rdI32(data, 8 + i * 8);
        lumps[i].length = rdI32(data, 8 + i * 8 + 4);
    }

    // ---- parse textures -----------------------------------------------------
    int numTextures = 0;
    const u8* texBase = lumpInfo(data, dataSize, lumps[kLumpTextures], kTextureSize, numTextures);
    std::vector<std::string> textures;
    textures.reserve(numTextures);
    for (int i = 0; i < numTextures; ++i)
    {
        textures.push_back(rdStr(data, dataSize, (texBase - data) + i * kTextureSize));
    }

    // ---- parse lightmaps (lump 14: 128x128 RGB pages) -----------------------
    // Q3 lightmap pages are stored as raw, ungamma-corrected radiosity
    // samples — applied straight to the framebuffer they read flat/dark, so
    // every Q3-derived renderer re-brightens them on load (the classic
    // r_lightmapgamma/overbright-bits knob). kLightmapGamma < 1 brightens
    // midtones (out = (in/255)^gamma * 255); 1.0 would be a no-op.
    constexpr float kLightmapGamma = 0.9f;
    constexpr int kLmSize = 128 * 128 * 3;
    int numLightmaps = 0;
    const u8* lmBase = lumpInfo(data, dataSize, lumps[kLumpLightmaps], kLmSize, numLightmaps);
    std::vector<gl::Texture*> lightmapTextures;
    lightmapTextures.reserve(numLightmaps);
    u8 gammaLUT[256];
    for (int v = 0; v < 256; ++v)
    {
        float f = std::pow(static_cast<float>(v) / 255.f, kLightmapGamma) * 255.f;
        gammaLUT[v] = static_cast<u8>(f < 0.f ? 0.f : (f > 255.f ? 255.f : f));
    }
    for (int i = 0; i < numLightmaps; ++i)
    {
        const u8* src = lmBase + i * kLmSize;
        std::vector<u8> rgba(128 * 128 * 4);
        for (int p = 0; p < 128 * 128; ++p)
        {
            rgba[p * 4 + 0] = gammaLUT[src[p * 3 + 0]];
            rgba[p * 4 + 1] = gammaLUT[src[p * 3 + 1]];
            rgba[p * 4 + 2] = gammaLUT[src[p * 3 + 2]];
            rgba[p * 4 + 3] = 255;
        }
        std::string lmName = std::string(name) + "/lm_" + std::to_string(i);
        gl::Texture* lmTex = assets::AssetManager::instance().createTexture(lmName.c_str());
        if (lmTex)
        {
            lmTex->Load2D(rgba.data(), 128, 128, gl::TextureFormat::RGBA8);
            lmTex->SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
            lmTex->SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
            lightmapTextures.push_back(lmTex);
        }
    }
    gl::Log::Info("[BSP] %d lightmap pages", numLightmaps);

    // ---- parse vertices (Z-up -> Y-up swap) ---------------------------------
    int numVerts = 0;
    const u8* vertBase = lumpInfo(data, dataSize, lumps[kLumpVertices], kVertexSize, numVerts);
    std::vector<BspVertex> verts;
    verts.reserve(numVerts);
    for (int i = 0; i < numVerts; ++i)
    {
        int o = static_cast<int>((vertBase - data) + i * kVertexSize);
        BspVertex v;
        // Q3 is Z-up; swap Y and Z.  Vertex layout (44 bytes):
        //   pos(0,4,8)  uv(12,16)  lmuv(20,24)  normal(28,32,36)  color(40)
        v.position = Vec3(rdF32(data, o), rdF32(data, o + 8), rdF32(data, o + 4));
        v.normal   = Vec3(rdF32(data, o + 28), rdF32(data, o + 36), rdF32(data, o + 32));
        v.uv       = Vec2(rdF32(data, o + 12), rdF32(data, o + 16));
        v.lmuv     = Vec2(rdF32(data, o + 20), rdF32(data, o + 24));
        verts.push_back(v);
    }

    // ---- parse mesh vertices (int indices) ----------------------------------
    int numMeshVerts = 0;
    const u8* mvBase = lumpInfo(data, dataSize, lumps[kLumpMeshVerts], 4, numMeshVerts);
    std::vector<int> meshVerts;
    meshVerts.reserve(numMeshVerts);
    for (int i = 0; i < numMeshVerts; ++i)
    {
        meshVerts.push_back(rdI32(data, static_cast<int>((mvBase - data) + i * 4)));
    }

    // ---- parse faces --------------------------------------------------------
    int numFaces = 0;
    const u8* faceBase = lumpInfo(data, dataSize, lumps[kLumpFaces], kFaceSize, numFaces);
    std::vector<Face> faces;
    faces.reserve(numFaces);
    for (int i = 0; i < numFaces; ++i)
    {
        int o = static_cast<int>((faceBase - data) + i * kFaceSize);
        Face f;
        f.textureIdx = rdI32(data, o);
        f.faceType = rdI32(data, o + 8);
        f.firstVert = rdI32(data, o + 12);
        f.numVerts = rdI32(data, o + 16);
        f.firstMeshVert = rdI32(data, o + 20);
        f.numMeshVerts = rdI32(data, o + 24);
        f.lm_index     = rdI32(data, o + 28);
        f.lm_start[0]  = rdI32(data, o + 32);
        f.lm_start[1]  = rdI32(data, o + 36);
        f.lm_size[0]   = rdI32(data, o + 40);
        f.lm_size[1]   = rdI32(data, o + 44);
        f.patchW = rdI32(data, o + 96);
        f.patchH = rdI32(data, o + 100);
        faces.push_back(f);
    }

    // ---- group faces by texture index (preserve insertion order) ------------
    std::unordered_map<int, std::vector<int>> group;
    std::vector<int> order;
    for (int i = 0; i < numFaces; ++i)
    {
        int t = faces[i].textureIdx;
        if (group.find(t) == group.end()) order.push_back(t);
        group[t].push_back(i);
    }

    // ---- build engine geometry ---------------------------------------------
    Mesh* m = createMesh(name);
    if (!m) return nullptr;
    std::vector<MeshVertex> outVerts;
    std::vector<Vec2> outLmUvs; // parallel: lightmap UV per vertex
    std::vector<u32> outIndices;
    outVerts.reserve(numVerts * 2);
    outLmUvs.reserve(numVerts * 2);
    outIndices.reserve(meshVerts.size());

    const std::string texDir(textureDir ? textureDir : "");

    struct SurfDef { u32 start, count; int slot; BoundingBox bb; };
    std::vector<SurfDef> surfDefs;

    int surfIdx = 0;
    for (int ti : order)
    {
        const std::vector<int>& faceList = group[ti];
        const u32 startIndex = static_cast<u32>(outIndices.size());

        for (int fi : faceList)
        {
            const Face& f = faces[fi];

            if (f.faceType == kFacePolygon || f.faceType == kFaceMesh)
            {
                for (int t = 0; t < f.numMeshVerts; ++t)
                {
                    int mvi = f.firstMeshVert + t;
                    if (mvi < 0 || mvi >= (int)meshVerts.size()) continue;
                    int vIdx = f.firstVert + meshVerts[mvi];
                    if (vIdx >= 0 && vIdx < numVerts)
                    {
                        outIndices.push_back(static_cast<u32>(outVerts.size()));
                        outVerts.push_back(makeVertex(verts[vIdx]));
                        outLmUvs.push_back(verts[vIdx].lmuv);
                    }
                }
            }
            else if (f.faceType == kFacePatch)
            {
                for (int by = 0; by + 2 < f.patchH; by += 2)
                {
                    for (int bx = 0; bx + 2 < f.patchW; bx += 2)
                    {
                        BspVertex cp[9];
                        bool bad = false;
                        for (int py = 0; py < 3; ++py)
                        {
                            for (int px = 0; px < 3; ++px)
                            {
                                int vi = f.firstVert + (by + py) * f.patchW + (bx + px);
                                if (vi < 0 || vi >= numVerts) { bad = true; break; }
                                cp[py * 3 + px] = verts[vi];
                            }
                            if (bad) break;
                        }
                        if (bad) continue;

                        const int n = kPatchTess + 1;
                        u32 baseVert = static_cast<u32>(outVerts.size());
                        for (int yy = 0; yy < n; ++yy)
                            for (int xx = 0; xx < n; ++xx)
                            {
                                float s = static_cast<float>(xx) / kPatchTess;
                                float t = static_cast<float>(yy) / kPatchTess;
                                BspVertex ev = evalPatch(cp, s, t);
                                outVerts.push_back(makeVertex(ev));
                                outLmUvs.push_back(ev.lmuv);
                            }

                        for (int yy = 0; yy < kPatchTess; ++yy)
                            for (int xx = 0; xx < kPatchTess; ++xx)
                            {
                                u32 i0 = baseVert + (yy * n + xx);
                                u32 i1 = i0 + 1;
                                u32 i2 = i0 + n;
                                u32 i3 = i2 + 1;
                                outIndices.push_back(i0);
                                outIndices.push_back(i2);
                                outIndices.push_back(i1);
                                outIndices.push_back(i1);
                                outIndices.push_back(i2);
                                outIndices.push_back(i3);
                            }
                    }
                }
            }
        }

        const u32 indexCount = static_cast<u32>(outIndices.size()) - startIndex;
        if (indexCount == 0) continue;

        // Compute surface bounding box.
        Vec3 mn(MaxFloat, MaxFloat, MaxFloat);
        Vec3 mx(-MaxFloat, -MaxFloat, -MaxFloat);
        for (u32 i = startIndex; i < (u32)outIndices.size(); ++i)
        {
            const Vec3& p = outVerts[outIndices[i]].position;
            mn = mn.Min(p);
            mx = mx.Max(p);
        }

        // Create material for this texture group.
        std::string texName = (ti >= 0 && ti < (int)textures.size() && !textures[ti].empty())
                                  ? textures[ti]
                                  : ("surf_" + std::to_string(surfIdx));
        Material* mat = new Material();
        gl::Texture* tex = tryLoadTex(texDir, texName);
        mat->diffuse = tex;
        mat->double_sided = false; // winding swap already fixes CW→CCW
        mat->lightmapped = true;        mat->detail_scale = 1.0f; // lightmap, not tiled detail
        // Assign the lightmap page used by the first face in this group.
        if (!faceList.empty())
        {
            int lmIdx = faces[faceList[0]].lm_index;
            if (lmIdx >= 0 && lmIdx < numLightmaps)
                mat->detail = lightmapTextures[lmIdx];
        }
        out_mats.push_back(mat);

        surfDefs.push_back({startIndex, indexCount, surfIdx, BoundingBox(mn, mx)});
        ++surfIdx;
    }

    if (outVerts.empty() || outIndices.empty())
    {
        gl::Log::Error("[BSP] no supported geometry in '%s'", path);
        return nullptr;
    }

    // Q3 BSP faces use CW winding; OpenGL expects CCW. Flip every triangle.
    // for (size_t i = 0; i + 2 < outIndices.size(); i += 3)
    //     std::swap(outIndices[i + 1], outIndices[i + 2]);

    // set_data first (clears surfaces), then add_surface, then upload.
    m->set_data(outVerts.data(), static_cast<u32>(outVerts.size()), outIndices.data(),
                static_cast<u32>(outIndices.size()));
    for (const SurfDef& s : surfDefs)
        m->add_surface(s.start, s.count, s.slot, s.bb);
    m->set_lightmap_uvs(outLmUvs.data(), static_cast<u32>(outLmUvs.size()));
    // Mesh takes ownership from here — freed with it in AssetManager::clear()/
    // ~Mesh(); out_mats stays a valid view for the caller (texture tweaks,
    // etc.) but must not be deleted by it.
    m->set_owned_materials(out_mats);
    m->upload();

    gl::Log::Info("[BSP] '%s': verts=%u tris=%u surfaces=%u textures=%u lm_uvs=%d lm_pages=%d", path,
                  (u32)outVerts.size(), (u32)(outIndices.size() / 3), (u32)surfDefs.size(),
                  (u32)textures.size(), (int)outLmUvs.size(), numLightmaps);
    return m;
}
