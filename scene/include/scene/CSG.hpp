#pragma once

#include "scene/Math.hpp"

class Mesh;

// CSG — Constructive Solid Geometry (boolean ops via BSP-tree, base csg.js).
// Operates purely on GEOMETRY: like every other loader, the result is
// owned by the AssetManager (freed in clear()/dtor) under `name` — returns
// the existing mesh if `name` was already built. Surfaces are per
// material_slot; B's slots come after A's (offset), so the caller must
// combine A's and B's material lists into one for rendering. Requires
// watertight meshes, CCW faces.
namespace CSG
{
enum class Operation
{
    Union,
    Difference,
    Intersection
};

struct Options
{
    bool smoothNormals = true; // recompute smooth normals on the result (else flat)
};

Mesh* makeUnion(const char* name, const Mesh& A, const Mesh& B, const Mat4& matA = Mat4::Identity(),
               const Mat4& matB = Mat4::Identity(), const Options& opts = {});
Mesh* makeDifference(const char* name, const Mesh& A, const Mesh& B, const Mat4& matA = Mat4::Identity(),
                    const Mat4& matB = Mat4::Identity(), const Options& opts = {});
Mesh* makeIntersection(const char* name, const Mesh& A, const Mesh& B, const Mat4& matA = Mat4::Identity(),
                      const Mat4& matB = Mat4::Identity(), const Options& opts = {});
Mesh* compute(const char* name, Operation op, const Mesh& A, const Mesh& B, const Mat4& matA = Mat4::Identity(),
             const Mat4& matB = Mat4::Identity(), const Options& opts = {});

Mesh* makeSymmetricDifference(const char* name, const Mesh& A, const Mesh& B, const Mat4& matA = Mat4::Identity(),
                             const Mat4& matB = Mat4::Identity(), const Options& opts = {});
Mesh* makeInvert(const char* name, const Mesh& A, const Mat4& matA = Mat4::Identity(), const Options& opts = {});

struct SplitResult
{
    Mesh* front = nullptr;
    Mesh* back = nullptr;
};
// Builds two meshes (may be null if empty) under `frontName`/`backName`.
SplitResult makeSplit(const char* frontName, const char* backName, const Mesh& A, const Vec3& planeNormal,
                      float planeDist, const Mat4& matA = Mat4::Identity(), const Options& opts = {});

Mesh* makeHollow(const char* name, const Mesh& A, float thickness, const Mat4& matA = Mat4::Identity(),
                const Options& opts = {});
} // namespace CSG
