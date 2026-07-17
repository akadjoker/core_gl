#include "scene/VolumeNoise.hpp"
#include <cmath>
#include <ctime>

namespace volume
{

namespace
{
constexpr float F3 = 1.0f / 3.0f;
constexpr float G3 = 1.0f / 6.0f;

const Vec3 grad3[12] = {
    Vec3(1, 1, 0),  Vec3(-1, 1, 0),  Vec3(1, -1, 0),  Vec3(-1, -1, 0),
    Vec3(1, 0, 1),  Vec3(-1, 0, 1),  Vec3(1, 0, -1),  Vec3(-1, 0, -1),
    Vec3(0, 1, 1),  Vec3(0, -1, 1),  Vec3(0, 1, -1),  Vec3(0, -1, -1),
};
} // namespace

unsigned long SimplexNoise::random()
{
    // XORShift, per http://www.jstatsoft.org/v08/i14/paper
    mSeed ^= mSeed << 13;
    mSeed = mSeed >> 17;
    return mSeed ^= mSeed << 5;
}

void SimplexNoise::init(unsigned long definedSeed)
{
    mSeed = (long)definedSeed;
    short p[256];
    for (int i = 0; i < 256; ++i) p[i] = (short)(random() % 256);

    for (int i = 0; i < 512; ++i)
    {
        perm[i] = p[i & 255];
        permMod12[i] = (short)(perm[i] % 12);
    }
}

SimplexNoise::SimplexNoise() { init((unsigned long)time(nullptr)); }
SimplexNoise::SimplexNoise(unsigned long definedSeed) { init(definedSeed); }

float SimplexNoise::noise(float xIn, float yIn, float zIn) const
{
    float n0, n1, n2, n3; // noise contributions from the four corners

    // Skew the input space to determine which simplex cell we're in.
    float s = (xIn + yIn + zIn) * F3;
    int i = (int)floorf(xIn + s);
    int j = (int)floorf(yIn + s);
    int k = (int)floorf(zIn + s);
    float t = (i + j + k) * G3;
    float X0 = i - t; // unskew the cell origin back to (x,y,z) space
    float Y0 = j - t;
    float Z0 = k - t;
    float x0 = xIn - X0; // the x,y,z distances from the cell origin
    float y0 = yIn - Y0;
    float z0 = zIn - Z0;

    // For the 3D case, the simplex shape is a slightly irregular tetrahedron.
    // Determine which simplex we are in.
    int i1, j1, k1; // offsets for second corner of simplex in (i,j,k) coords
    int i2, j2, k2; // offsets for third corner of simplex in (i,j,k) coords
    if (x0 >= y0)
    {
        if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }        // X Y Z order
        else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }   // X Z Y order
        else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }                // Z X Y order
    }
    else
    {
        if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }         // Z Y X order
        else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }    // Y Z X order
        else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }                // Y X Z order
    }

    // A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
    // a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), and
    // a step of (0,0,1) in (i,j,k) means a step of (-c,-c,1-c) in (x,y,z),
    // where c = 1/6.
    float x1 = x0 - i1 + G3; // offsets for second corner in (x,y,z) coords
    float y1 = y0 - j1 + G3;
    float z1 = z0 - k1 + G3;
    float x2 = x0 - i2 + 2.0f * G3; // offsets for third corner in (x,y,z) coords
    float y2 = y0 - j2 + 2.0f * G3;
    float z2 = z0 - k2 + 2.0f * G3;
    float x3 = x0 - 1.0f + 3.0f * G3; // offsets for last corner in (x,y,z) coords
    float y3 = y0 - 1.0f + 3.0f * G3;
    float z3 = z0 - 1.0f + 3.0f * G3;

    // Work out the hashed gradient indices of the four simplex corners.
    int ii = i & 255;
    int jj = j & 255;
    int kk = k & 255;
    int gi0 = permMod12[ii + perm[jj + perm[kk]]];
    int gi1 = permMod12[ii + i1 + perm[jj + j1 + perm[kk + k1]]];
    int gi2 = permMod12[ii + i2 + perm[jj + j2 + perm[kk + k2]]];
    int gi3 = permMod12[ii + 1 + perm[jj + 1 + perm[kk + 1]]];

    // Calculate the contribution from the four corners.
    float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
    if (t0 < 0) n0 = 0.0f;
    else { t0 *= t0; n0 = t0 * t0 * dot(grad3[gi0], x0, y0, z0); }

    float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
    if (t1 < 0) n1 = 0.0f;
    else { t1 *= t1; n1 = t1 * t1 * dot(grad3[gi1], x1, y1, z1); }

    float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
    if (t2 < 0) n2 = 0.0f;
    else { t2 *= t2; n2 = t2 * t2 * dot(grad3[gi2], x2, y2, z2); }

    float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
    if (t3 < 0) n3 = 0.0f;
    else { t3 *= t3; n3 = t3 * t3 * dot(grad3[gi3], x3, y3, z3); }

    // Add contributions from each corner to get the final noise value.
    // The result is scaled to stay just inside [-1,1].
    return 32.0f * (n0 + n1 + n2 + n3);
}

} // namespace volume
