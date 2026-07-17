#pragma once

#include "scene/Math.hpp"

// Standalone volumetric mesh generation: a Source is a density function
// (positive = inside the solid, negative = outside, zero = the surface);
// VolumeMesher.hpp extracts a Mesh* from one via marching cubes. Useful for
// caves/tunnels (Source = rock minus a chain of spheres, or noise-perturbed
// rock) and smooth CSG blobs (VolumeCSGSource.hpp) — unlike CSG.hpp, which
// booleans two POLYGON meshes and gets hard edges, this booleans the DENSITY
// FIELD itself, so the result blends smoothly (metaballs-style).
//
// Ported from OgreVolumeSource.h (Ogre::Volume::Source),
// trimmed to the density/gradient interface — grid file serialization and
// ray marching aren't needed for this standalone path and were dropped.
namespace volume
{

class Source
{
public:
    virtual ~Source() {}

    // xyz = gradient (unnormalized, points away from the surface toward
    // the outside), w = density at position.
    virtual Vec4 getValueAndGradient(const Vec3& position) const = 0;
    // Density only, when the gradient isn't needed (cheaper).
    virtual float getValue(const Vec3& position) const = 0;
};

// Marches along `ray` looking for the surface (density == 0), stepping by
// density/local-gradient-scale each iteration (not true SDF sphere
// tracing — `src`'s density isn't guaranteed distance-normalized — but
// converges the same way in practice). Useful for "what is the camera
// looking at" queries (dig-point targeting, picking) without needing a
// mesh: works directly off the density field. Returns false (leaves
// outHit untouched) if nothing is found within maxDistance/maxIterations.
bool rayMarch(const Source& src, const Ray& ray, Vec3& outHit, float maxDistance = 100.0f,
              size_t maxIterations = 256);

} // namespace volume
