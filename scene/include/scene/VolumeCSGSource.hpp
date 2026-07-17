#pragma once

#include "scene/VolumeNoise.hpp"
#include "scene/VolumeSource.hpp"

// Procedural CSG density sources — combine primitives (sphere/plane/cube)
// with boolean ops evaluated on the DENSITY FIELD, so the result blends
// smoothly at the seams (metaballs-style), unlike CSG.hpp's polygon
// booleans. CSGNoiseSource adds simplex-noise octaves to any source —
// rough rock, caves carved by subtracting a noisy blob from a solid.
// Ported from OgreVolumeCSGSource.h (Ogre::Volume::CSG*).
namespace volume
{

class CSGSphereSource : public Source
{
public:
    CSGSphereSource(float r, const Vec3& center) : mR(r), mCenter(center) {}
    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;

private:
    float mR;
    Vec3 mCenter;
};

class CSGPlaneSource : public Source
{
public:
    CSGPlaneSource(float d, const Vec3& normal) : mD(d), mNormal(normal.normalized()) {}
    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;

private:
    float mD;
    Vec3 mNormal;
};

// Axis-aligned, unrotated box.
class CSGCubeSource : public Source
{
public:
    CSGCubeSource(const Vec3& boxMin, const Vec3& boxMax) : mMin(boxMin), mMax(boxMax) {}
    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;

private:
    float distanceTo(const Vec3& position) const;
    Vec3 mMin, mMax;
};

// Non-owning: sources referenced by CSG combinators must outlive them.
class CSGOperationSource : public Source
{
public:
    CSGOperationSource(const Source* a, const Source* b) : mA(a), mB(b) {}

protected:
    const Source* mA;
    const Source* mB;
};

class CSGUnionSource : public CSGOperationSource
{
public:
    using CSGOperationSource::CSGOperationSource;
    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;
};

class CSGIntersectionSource : public CSGOperationSource
{
public:
    using CSGOperationSource::CSGOperationSource;
    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;
};

class CSGDifferenceSource : public CSGOperationSource
{
public:
    using CSGOperationSource::CSGOperationSource;
    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;
};

class CSGUnarySource : public Source
{
public:
    explicit CSGUnarySource(const Source* src) : mSrc(src) {}

protected:
    const Source* mSrc;
};

class CSGNegateSource : public CSGUnarySource
{
public:
    using CSGUnarySource::CSGUnarySource;
    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;
};

class CSGScaleSource : public CSGUnarySource
{
public:
    CSGScaleSource(const Source* src, float scale) : CSGUnarySource(src), mScale(scale) {}
    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;

private:
    float mScale;
};

// Adds noise octaves to `src`'s density. `frequencies`/`amplitudes` are
// non-owning (caller keeps them alive) — one entry per octave.
class CSGNoiseSource : public CSGUnarySource
{
public:
    CSGNoiseSource(const Source* src, const float* frequencies, const float* amplitudes, size_t numOctaves,
                  unsigned long seed);
    CSGNoiseSource(const Source* src, const float* frequencies, const float* amplitudes, size_t numOctaves);

    Vec4 getValueAndGradient(const Vec3& position) const override;
    float getValue(const Vec3& position) const override;
    long getSeed() const { return mNoise.getSeed(); }

private:
    void setup();
    float internalValue(const Vec3& position) const;

    const float* mFrequencies;
    const float* mAmplitudes;
    size_t mNumOctaves;
    SimplexNoise mNoise;
    float mGradientOff = 0.0f;
};

} // namespace volume
