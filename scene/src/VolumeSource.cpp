#include "scene/VolumeSource.hpp"
#include <cmath>

namespace volume
{

bool rayMarch(const Source& src, const Ray& ray, Vec3& outHit, float maxDistance, size_t maxIterations)
{
    Vec3 start = ray.origin;
    Vec3 end = ray.origin + ray.direction * maxDistance;
    Vec3 cur = start;

    // estimate how fast density changes per unit distance near the start,
    // so the first step is roughly the right size regardless of scale
    Vec4 startVal = src.getValueAndGradient(start);
    Vec3 gradient(startVal.x, startVal.y, startVal.z);
    Vec3 sampleEnd = start + gradient.normalized();
    float sample = src.getValue(sampleEnd);
    float densityScale = 1.0f / fabsf(sample - startVal.w) * 2.0f;

    float densityCur = src.getValue(cur);
    Vec3 prev(0, 0, 0), prevPrev(0, 0, 0);
    float totalLength = (start - end).length();
    bool atEnd = false;

    size_t count = 0;
    while (fabsf(densityCur) > 0.01f && !atEnd)
    {
        cur += ray.direction * -1.0f * (densityCur / densityScale);

        // bounce detection: stuck oscillating back and forth, widen the step
        if ((cur - prevPrev).length() < 0.0001f) densityScale *= 2.0f;
        prevPrev = prev;
        prev = cur;

        densityCur = src.getValue(cur);
        if ((start - cur).length() >= totalLength) atEnd = true;

        ++count;
        if (count > maxIterations) break;
    }

    if (fabsf(densityCur) <= 0.01f)
    {
        outHit = cur;
        return true;
    }
    return false;
}

} // namespace volume
