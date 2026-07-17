#include "scene/VolumeGridSource.hpp"
#include "scene/ByteArray.hpp"
#include "scene/Filesystem.hpp"
#include <algorithm>
#include <cmath>
#include <coregl/gl_log.hpp>
#include <cstdio>

namespace volume
{

namespace
{
constexpr gl::u32 kMagic = 0x44524756u; // "VGRD" little-endian
constexpr gl::u32 kVersion = 1;
} // namespace

GridSource::GridSource(int width, int height, int depth, const Vec3& origin, float cellSize, float initialValue,
                       bool trilinear, bool sobelGradient)
    : mWidth(width < 1 ? 1 : width), mHeight(height < 1 ? 1 : height), mDepth(depth < 1 ? 1 : depth),
      mOrigin(origin), mCellSize(cellSize > 0.0f ? cellSize : 1.0f), mTrilinear(trilinear),
      mSobelGradient(sobelGradient), mData((size_t)mWidth * mHeight * mDepth, initialValue)
{
}

Vec3 GridSource::toCellSpace(const Vec3& position) const
{
    return (position - mOrigin) * (1.0f / mCellSize);
}

float GridSource::getVoxel(int x, int y, int z) const
{
    x = std::min(std::max(x, 0), mWidth - 1);
    y = std::min(std::max(y, 0), mHeight - 1);
    z = std::min(std::max(z, 0), mDepth - 1);
    return mData[index(x, y, z)];
}

void GridSource::setVoxel(int x, int y, int z, float value)
{
    if (x < 0 || y < 0 || z < 0 || x >= mWidth || y >= mHeight || z >= mDepth) return;
    mData[index(x, y, z)] = value;
}

// central difference, same scheme as the classic marching-cubes paper,
// with an optional Sobel-style blur mixed in for a smoother normal
Vec3 GridSource::gradientAt(int x, int y, int z) const
{
    if (mSobelGradient)
    {
        return Vec3((getVoxel(x + 1, y - 1, z) - getVoxel(x - 1, y - 1, z)) +
                       2.0f * (getVoxel(x + 1, y, z) - getVoxel(x - 1, y, z)) +
                       (getVoxel(x + 1, y + 1, z) - getVoxel(x - 1, y + 1, z)),
                   (getVoxel(x, y + 1, z - 1) - getVoxel(x, y - 1, z - 1)) +
                       2.0f * (getVoxel(x, y + 1, z) - getVoxel(x, y - 1, z)) +
                       (getVoxel(x, y + 1, z + 1) - getVoxel(x, y - 1, z + 1)),
                   (getVoxel(x - 1, y, z + 1) - getVoxel(x - 1, y, z - 1)) +
                       2.0f * (getVoxel(x, y, z + 1) - getVoxel(x, y, z - 1)) +
                       (getVoxel(x + 1, y, z + 1) - getVoxel(x + 1, y, z - 1))) *
              0.25f;
    }
    return Vec3(getVoxel(x + 1, y, z) - getVoxel(x - 1, y, z), getVoxel(x, y + 1, z) - getVoxel(x, y - 1, z),
               getVoxel(x, y, z + 1) - getVoxel(x, y, z - 1));
}

float GridSource::getValue(const Vec3& position) const
{
    Vec3 c = toCellSpace(position);
    if (!mTrilinear)
    {
        int x = (int)floorf(c.x + 0.5f), y = (int)floorf(c.y + 0.5f), z = (int)floorf(c.z + 0.5f);
        return getVoxel(x, y, z);
    }

    int x0 = (int)floorf(c.x), y0 = (int)floorf(c.y), z0 = (int)floorf(c.z);
    int x1 = x0 + 1, y1 = y0 + 1, z1 = z0 + 1;
    float dX = c.x - (float)x0, dY = c.y - (float)y0, dZ = c.z - (float)z0;

    float f000 = getVoxel(x0, y0, z0), f100 = getVoxel(x1, y0, z0);
    float f010 = getVoxel(x0, y1, z0), f001 = getVoxel(x0, y0, z1);
    float f101 = getVoxel(x1, y0, z1), f011 = getVoxel(x0, y1, z1);
    float f110 = getVoxel(x1, y1, z0), f111 = getVoxel(x1, y1, z1);

    float oneMinX = 1.0f - dX, oneMinY = 1.0f - dY, oneMinZ = 1.0f - dZ;
    float oneMinXoneMinY = oneMinX * oneMinY, dXOneMinY = dX * oneMinY;

    return oneMinZ * (f000 * oneMinXoneMinY + f100 * dXOneMinY + f010 * oneMinX * dY) +
          dZ * (f001 * oneMinXoneMinY + f101 * dXOneMinY + f011 * oneMinX * dY) +
          dX * dY * (f110 * oneMinZ + f111 * dZ);
}

Vec4 GridSource::getValueAndGradient(const Vec3& position) const
{
    Vec3 c = toCellSpace(position);
    Vec3 gradient;
    if (!mTrilinear)
    {
        int x = (int)floorf(c.x + 0.5f), y = (int)floorf(c.y + 0.5f), z = (int)floorf(c.z + 0.5f);
        gradient = gradientAt(x, y, z) * -1.0f;
    }
    else
    {
        int x0 = (int)floorf(c.x), y0 = (int)floorf(c.y), z0 = (int)floorf(c.z);
        int x1 = x0 + 1, y1 = y0 + 1, z1 = z0 + 1;
        float dX = c.x - (float)x0, dY = c.y - (float)y0, dZ = c.z - (float)z0;

        Vec3 f000 = gradientAt(x0, y0, z0), f100 = gradientAt(x1, y0, z0);
        Vec3 f010 = gradientAt(x0, y1, z0), f001 = gradientAt(x0, y0, z1);
        Vec3 f101 = gradientAt(x1, y0, z1), f011 = gradientAt(x0, y1, z1);
        Vec3 f110 = gradientAt(x1, y1, z0), f111 = gradientAt(x1, y1, z1);

        float oneMinX = 1.0f - dX, oneMinY = 1.0f - dY, oneMinZ = 1.0f - dZ;
        float oneMinXoneMinY = oneMinX * oneMinY, dXOneMinY = dX * oneMinY;

        gradient = (f000 * oneMinXoneMinY + f100 * dXOneMinY + f010 * oneMinX * dY) * oneMinZ +
                  (f001 * oneMinXoneMinY + f101 * dXOneMinY + f011 * oneMinX * dY) * dZ +
                  (f110 * oneMinZ + f111 * dZ) * (dX * dY);
        gradient = gradient * -1.0f;
    }
    return Vec4(gradient.x, gradient.y, gradient.z, getValue(position));
}

void GridSource::fill(const Source& src)
{
    for (int z = 0; z < mDepth; ++z)
        for (int y = 0; y < mHeight; ++y)
            for (int x = 0; x < mWidth; ++x)
            {
                Vec3 pos = mOrigin + Vec3((float)x, (float)y, (float)z) * mCellSize;
                mData[index(x, y, z)] = src.getValue(pos);
            }
}

void GridSource::combineWithSource(Operation op, const Source& brush, const Vec3& center, float radius)
{
    Vec3 c = toCellSpace(center);
    float rCells = radius / mCellSize;
    int xStart = std::max(0, (int)floorf(c.x - rCells)), xEnd = std::min(mWidth, (int)ceilf(c.x + rCells));
    int yStart = std::max(0, (int)floorf(c.y - rCells)), yEnd = std::min(mHeight, (int)ceilf(c.y + rCells));
    int zStart = std::max(0, (int)floorf(c.z - rCells)), zEnd = std::min(mDepth, (int)ceilf(c.z + rCells));

    for (int z = zStart; z < zEnd; ++z)
        for (int y = yStart; y < yEnd; ++y)
            for (int x = xStart; x < xEnd; ++x)
            {
                Vec3 pos = mOrigin + Vec3((float)x, (float)y, (float)z) * mCellSize;
                float a = getVoxel(x, y, z); // raw cell value, no interpolation
                float b = brush.getValue(pos);
                float result;
                switch (op)
                {
                case Operation::Union: result = std::max(a, b); break;
                case Operation::Difference: result = std::min(a, -b); break;
                case Operation::Intersection: result = std::min(a, b); break;
                default: result = a; break;
                }
                setVoxel(x, y, z, result);
            }
}

bool GridSource::save(const char* path) const
{
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        gl::Log::Error("[VolumeGridSource] cannot open '%s' for writing", path);
        return false;
    }
    gl::u32 magic = kMagic, version = kVersion;
    gl::u32 w = (gl::u32)mWidth, h = (gl::u32)mHeight, d = (gl::u32)mDepth;
    bool ok = fwrite(&magic, sizeof(magic), 1, f) == 1 && fwrite(&version, sizeof(version), 1, f) == 1 &&
             fwrite(&mOrigin, sizeof(mOrigin), 1, f) == 1 && fwrite(&mCellSize, sizeof(mCellSize), 1, f) == 1 &&
             fwrite(&w, sizeof(w), 1, f) == 1 && fwrite(&h, sizeof(h), 1, f) == 1 &&
             fwrite(&d, sizeof(d), 1, f) == 1 &&
             fwrite(mData.data(), sizeof(float), mData.size(), f) == mData.size();
    fclose(f);
    if (!ok) gl::Log::Error("[VolumeGridSource] write failed: '%s'", path);
    return ok;
}

GridSource* GridSource::load(const char* path)
{
    scene::ByteArray data;
    if (!fs::getFilesystem().readFile(path, data))
    {
        gl::Log::Error("[VolumeGridSource] cannot read '%s'", path);
        return nullptr;
    }
    data.resetCursor();

    gl::u32 magic = 0, version = 0;
    Vec3 origin;
    float cellSize = 0.0f;
    gl::u32 w = 0, h = 0, d = 0;
    if (!data.readU32(magic) || magic != kMagic || !data.readU32(version) || version != kVersion ||
        !data.readF32(origin.x) || !data.readF32(origin.y) || !data.readF32(origin.z) ||
        !data.readF32(cellSize) || !data.readU32(w) || !data.readU32(h) || !data.readU32(d))
    {
        gl::Log::Error("[VolumeGridSource] '%s' is not a valid grid file", path);
        return nullptr;
    }

    GridSource* grid = new GridSource((int)w, (int)h, (int)d, origin, cellSize);
    size_t count = (size_t)w * h * d;
    if (!data.readF32Array(grid->mData.data(), (gl::u32)count))
    {
        gl::Log::Error("[VolumeGridSource] '%s' is truncated", path);
        delete grid;
        return nullptr;
    }
    return grid;
}

} // namespace volume
