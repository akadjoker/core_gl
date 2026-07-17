#pragma once

#include "scene/Math.hpp"

// 3D Simplex noise
// (Ogre::Volume::SimplexNoise), itself ported from the public-domain
// reference at http://webstaff.itn.liu.se/~stegu/simplexnoise/SimplexNoise.java
// (Stefan Gustavson). Used by VolumeCSGSource's CSGNoiseSource to perturb a
// density field — rough rock surfaces, caves.
namespace volume
{

class SimplexNoise
{
public:
    SimplexNoise();                        // random seed (time-based)
    explicit SimplexNoise(unsigned long definedSeed);

    // 3D noise in roughly [-1, 1].
    float noise(float xIn, float yIn, float zIn) const;

    long getSeed() const { return mSeed; }

private:
    void init(unsigned long definedSeed);
    unsigned long random();
    float dot(const Vec3& g, float x, float y, float z) const { return g.x * x + g.y * y + g.z * z; }

    long mSeed = 0;
    short perm[512] = {};
    short permMod12[512] = {};
};

} // namespace volume
