#pragma once

#include "scene/VolumeSource.hpp"
#include <vector>

// A discrete density grid — the real "paintable voxel" source, as opposed
// to VolumeCSGSource.hpp's procedural trees (spheres/boxes/noise combined
// algebraically). Values are stored per-cell and sampled with trilinear
// interpolation, so the extracted surface stays smooth despite the
// discrete storage. Editing (combineWithSource) writes results straight
// into the affected cells, so cost is O(edited region) and constant per
// edit — unlike chaining more CSGUnionSource/CSGDifferenceSource nodes,
// whose evaluation cost grows with edit count.
//
// Ported from greVolumeGridSource.h +
// OgreVolumeHalfFloatGridSource.h (Ogre::Volume::GridSource/
// HalfFloatGridSource), collapsed into one concrete float-storage class
// (the half-float packing was a memory optimization, not a capability —
// worth revisiting if grid memory becomes a real problem) and given an
// explicit world-space origin/cellSize so it can be used directly like
// CSGSphereSource/CSGCubeSource instead of needing an external transform.
namespace volume
{

class GridSource : public Source
{
public:
    // width x height x depth cells, spanning [origin, origin +
    // Vec3(w,h,d)*cellSize] in world space. Every cell starts at
    // `initialValue` (negative = starts as air, positive = starts solid).
    GridSource(int width, int height, int depth, const Vec3& origin, float cellSize, float initialValue = -1.0f,
              bool trilinear = true, bool sobelGradient = false);

    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;

    int width() const { return mWidth; }
    int height() const { return mHeight; }
    int depth() const { return mDepth; }
    const Vec3& origin() const { return mOrigin; }
    float cellSize() const { return mCellSize; }
    Vec3 maxCorner() const { return mOrigin + Vec3((float)mWidth, (float)mHeight, (float)mDepth) * mCellSize; }

    // direct cell access (indices clamped to the grid, never out of bounds)
    float getVoxel(int x, int y, int z) const;
    void setVoxel(int x, int y, int z, float value);

    // Bakes `src`'s value into every cell directly (no blending) — use
    // once to seed the grid from a procedural CSGSource tree.
    void fill(const Source& src);

    enum class Operation
    {
        Union,
        Difference,
        Intersection
    };
    // "Brush": bakes `op(this, brush)` into the grid within `radius` of
    // `center` (world space) — e.g. Difference with a sphere digs a hole,
    // Union with one builds. Only touches the affected cells (a bounding
    // box around center/radius), so cost doesn't grow with total edit
    // count the way chaining CSG nodes does.
    void combineWithSource(Operation op, const Source& brush, const Vec3& center, float radius);

    // Binary format (uncompressed, no version negotiation beyond the magic
    // check — this is a save file for one engine version, not a
    // cross-version asset format): magic "VGRD" + version u32, origin (3
    // floats) + cellSize (float), width/height/depth (3 u32), then
    // width*height*depth raw floats. Ported in spirit from tmp/Volume's
    // Source::serialize()/HalfFloatGridSource's file ctor, adapted to
    // plain fopen/fwrite (matches exporter/mesh2h3d's Stream.cpp — the
    // Filesystem abstraction is read-only, for loading assets) and
    // scene::ByteArray/fs::Filesystem for loading.
    bool save(const char* path) const;
    // Caller owns the returned pointer; nullptr on failure (logged).
    static GridSource* load(const char* path);

private:
    Vec3 gradientAt(int x, int y, int z) const;
    // (gx, gy, gz) = position in CELL space (position - origin) / cellSize,
    // NOT clamped — callers doing trilinear sampling need the fractional part.
    Vec3 toCellSpace(const Vec3& position) const;
    size_t index(int x, int y, int z) const { return ((size_t)z * mHeight + y) * mWidth + x; }

    int mWidth, mHeight, mDepth;
    Vec3 mOrigin;
    float mCellSize;
    bool mTrilinear;
    bool mSobelGradient;
    std::vector<float> mData;
};

} // namespace volume
