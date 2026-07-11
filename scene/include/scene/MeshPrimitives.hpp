#pragma once

class Mesh;

// Procedural primitives: fill `out` with positions/normals/uvs/indices and
// one surface (bounds included), CPU-side — the owner uploads. Normal use
// is through AssetManager::createCube/Sphere/... which also owns the Mesh.
namespace primitives
{
// axis-aligned box centered at the origin
void cube(Mesh& out, float sizeX, float sizeY, float sizeZ);
// XZ plane at y=0, `tiles` repeats of the uv over the whole plane.
// segX/segZ subdivide it (Irrlicht-style detail plane) — needed if you'll
// displace it per-vertex afterward (hills) or want per-quad lighting detail.
void plane(Mesh& out, float width, float depth, float uvTiles = 1.f, int segX = 1,
          int segZ = 1);
// same plane, displaced per-vertex by heightFn(worldX, worldZ) and with
// recomputed normals — a quick "hills" ground without a full TerrainNode
void hills_plane(Mesh& out, float width, float depth, int segX, int segZ,
                 float (*heightFn)(float x, float z), float uvTiles = 1.f);
// brute-force single-mesh terrain from a heightmap: heights[z*w+i] in
// world units, cellSize = world units between samples, one static mesh
// (no LOD/paging — for small patches, props, or previews; see
// TerrainPagingNode for open-world streaming terrain)
void heightfield(Mesh& out, const float* heights, int w, int h, float cellSize,
                 float uvTiles = 1.f);
// uv sphere centered at the origin
void sphere(Mesh& out, float radius, int rings = 16, int slices = 24);
// Y-axis cylinder, base at y=0, capped
void cylinder(Mesh& out, float radius, float height, int slices = 24);
// Y-axis cone, base at y=0, apex at y=height, capped
void cone(Mesh& out, float radius, float height, int slices = 24);
// Y-axis capsule: a cylindrical body of `height` (center to center of the
// two hemisphere caps) capped by hemispheres of `radius` — total height is
// height + 2*radius, base at y=0
void capsule(Mesh& out, float radius, float height, int rings = 8, int slices = 24);
} // namespace primitives
