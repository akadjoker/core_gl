#include "scene/VolumeCSGSource.hpp"
#include <algorithm>
#include <cmath>

namespace volume
{

// ---- CSGSphereSource ----

Vec4 CSGSphereSource::getValueAndGradient(const Vec3& position) const
{
    Vec3 g = position - mCenter;
    float len = g.length();
    return Vec4(g.x, g.y, g.z, mR - len);
}

float CSGSphereSource::getValue(const Vec3& position) const { return mR - (position - mCenter).length(); }

// ---- CSGPlaneSource ----

Vec4 CSGPlaneSource::getValueAndGradient(const Vec3& position) const
{
    return Vec4(mNormal.x, mNormal.y, mNormal.z, mD - Vec3::Dot(mNormal, position));
}

float CSGPlaneSource::getValue(const Vec3& position) const { return mD - Vec3::Dot(mNormal, position); }

// ---- CSGCubeSource ----

float CSGCubeSource::distanceTo(const Vec3& position) const
{
    Vec3 dMin = position - mMin;
    Vec3 dMax = mMax - position;

    if (dMin.x >= 0.0f && dMin.y >= 0.0f && dMin.z >= 0.0f && dMax.x >= 0.0f && dMax.y >= 0.0f && dMax.z >= 0.0f)
    {
        // Inside: distance to the nearest face.
        float d[6] = {dMin.x, dMin.y, dMin.z, dMax.x, dMax.y, dMax.z};
        float distance = d[0];
        for (int i = 1; i < 6; ++i)
            if (d[i] < distance) distance = d[i];
        return distance;
    }

    // Outside: negative distance to the box.
    Vec3 center = (mMin + mMax) * 0.5f;
    Vec3 extent = (mMax - mMin) * 0.5f;
    Vec3 nearest(std::max(0.0f, fabsf(position.x - center.x) - extent.x),
                std::max(0.0f, fabsf(position.y - center.y) - extent.y),
                std::max(0.0f, fabsf(position.z - center.z) - extent.z));
    return -nearest.length();
}

Vec4 CSGCubeSource::getValueAndGradient(const Vec3& position) const
{
    // Prewitt-style approximation — the real normal needs all 26 neighbor cases.
    Vec3 gradient(getValue(Vec3(position.x + 1.0f, position.y, position.z)) -
                     getValue(Vec3(position.x - 1.0f, position.y, position.z)),
                 getValue(Vec3(position.x, position.y + 1.0f, position.z)) -
                     getValue(Vec3(position.x, position.y - 1.0f, position.z)),
                 getValue(Vec3(position.x, position.y, position.z + 1.0f)) -
                     getValue(Vec3(position.x, position.y, position.z - 1.0f)));
    gradient = gradient.normalized() * -1.0f;
    return Vec4(gradient.x, gradient.y, gradient.z, distanceTo(position));
}

float CSGCubeSource::getValue(const Vec3& position) const { return distanceTo(position); }

// ---- CSGUnionSource / CSGIntersectionSource / CSGDifferenceSource ----

Vec4 CSGUnionSource::getValueAndGradient(const Vec3& position) const
{
    Vec4 a = mA->getValueAndGradient(position);
    Vec4 b = mB->getValueAndGradient(position);
    return a.w > b.w ? a : b;
}
float CSGUnionSource::getValue(const Vec3& position) const
{
    return std::max(mA->getValue(position), mB->getValue(position));
}

Vec4 CSGIntersectionSource::getValueAndGradient(const Vec3& position) const
{
    Vec4 a = mA->getValueAndGradient(position);
    Vec4 b = mB->getValueAndGradient(position);
    return a.w < b.w ? a : b;
}
float CSGIntersectionSource::getValue(const Vec3& position) const
{
    return std::min(mA->getValue(position), mB->getValue(position));
}

Vec4 CSGDifferenceSource::getValueAndGradient(const Vec3& position) const
{
    Vec4 a = mA->getValueAndGradient(position);
    Vec4 b = mB->getValueAndGradient(position) * -1.0f;
    return a.w < b.w ? a : b;
}
float CSGDifferenceSource::getValue(const Vec3& position) const
{
    return std::min(mA->getValue(position), -mB->getValue(position));
}

// ---- CSGNegateSource / CSGScaleSource ----

Vec4 CSGNegateSource::getValueAndGradient(const Vec3& position) const
{
    return mSrc->getValueAndGradient(position) * -1.0f;
}
float CSGNegateSource::getValue(const Vec3& position) const { return -mSrc->getValue(position); }

Vec4 CSGScaleSource::getValueAndGradient(const Vec3& position) const
{
    return mSrc->getValueAndGradient(position * (1.0f / mScale)) * mScale;
}
float CSGScaleSource::getValue(const Vec3& position) const
{
    return mSrc->getValue(position * (1.0f / mScale)) * mScale;
}

// ---- CSGNoiseSource ----

void CSGNoiseSource::setup()
{
    mGradientOff = fabsf(mFrequencies[0]);
    for (size_t i = 1; i < mNumOctaves; ++i)
        if (fabsf(mFrequencies[i]) < mGradientOff) mGradientOff = mFrequencies[i];
    mGradientOff /= 4.0f;
}

CSGNoiseSource::CSGNoiseSource(const Source* src, const float* frequencies, const float* amplitudes,
                              size_t numOctaves, unsigned long seed)
    : CSGUnarySource(src), mFrequencies(frequencies), mAmplitudes(amplitudes), mNumOctaves(numOctaves),
      mNoise(seed)
{
    setup();
}

CSGNoiseSource::CSGNoiseSource(const Source* src, const float* frequencies, const float* amplitudes,
                              size_t numOctaves)
    : CSGUnarySource(src), mFrequencies(frequencies), mAmplitudes(amplitudes), mNumOctaves(numOctaves)
{
    setup();
}

float CSGNoiseSource::internalValue(const Vec3& position) const
{
    float toAdd = 0.0f;
    for (size_t i = 0; i < mNumOctaves; ++i)
        toAdd += mNoise.noise(position.x * mFrequencies[i], position.y * mFrequencies[i],
                              position.z * mFrequencies[i]) *
                mAmplitudes[i];
    return mSrc->getValue(position) + toAdd;
}

Vec4 CSGNoiseSource::getValueAndGradient(const Vec3& position) const
{
    return Vec4(-(internalValue(Vec3(position.x + mGradientOff, position.y, position.z)) -
                 internalValue(Vec3(position.x - mGradientOff, position.y, position.z))),
               -(internalValue(Vec3(position.x, position.y + mGradientOff, position.z)) -
                 internalValue(Vec3(position.x, position.y - mGradientOff, position.z))),
               -(internalValue(Vec3(position.x, position.y, position.z + mGradientOff)) -
                 internalValue(Vec3(position.x, position.y, position.z - mGradientOff))),
               internalValue(position));
}

float CSGNoiseSource::getValue(const Vec3& position) const { return internalValue(position); }

} // namespace volume
