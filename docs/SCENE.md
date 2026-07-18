# Scene System Documentation

The **scene module** is a lightweight, node-based 3D engine built on top of the
coregl OpenGL abstraction. It provides a hierarchical scene graph, mesh and
material system, skeletal and morph animation, multi-format mesh loading,
procedural geometry, VFX nodes, five terrain variants, CSG/volume meshing,
behavior controllers, a math library, and utility systems for input, collision,
asset management, and virtual file I/O.

```cpp
#include <scene/Scene.hpp>
#include <scene/SceneRenderer.hpp>
#include <scene/Node3D.hpp>
#include <scene/Mesh.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Material.hpp>

scene::Scene           scn;
scene::SceneRenderer   renderer;

renderer.init();

// Build a simple scene
auto* cube_mesh = scn.create_mesh();
coregl::MeshPrimitives::cube(*cube_mesh);
cube_mesh->upload();

auto* mat = scn.create_material();
mat->base_color = { 0.8f, 0.4f, 0.2f };

auto* node = scn.root()->create_child<scene::MeshInstance>();
node->set_mesh(cube_mesh);
node->set_material(mat);
node->set_position({ 0, 0, -5 });

// Main loop
scn.ready();
while (running) {
    scn.update(dt);
    renderer.render(scn, width, height);
}
renderer.release();
scn.release_gpu();
```

---

## Table of Contents

1. [Core Hierarchy](#1-core-hierarchy)
   - [Node](#node)
   - [Node3D](#node3d)
   - [Scene](#scene)
   - [Camera3D](#camera3d)
   - [LightNode](#lightnode)
2. [Mesh & Material System](#2-mesh--material-system)
   - [Mesh](#mesh)
   - [Material](#material)
   - [MeshInstance](#meshinstance)
   - [MeshLoader](#meshloader)
   - [MeshPrimitives](#meshprimitives)
3. [Rendering Pipeline](#3-rendering-pipeline)
   - [SceneRenderer](#scenerenderer)
   - [SceneOctree](#sceneoctree)
4. [Skeletal Animation](#4-skeletal-animation)
   - [SkinnedMesh](#skinnedmesh)
   - [Skeleton](#skeleton)
   - [AnimationClip](#animationclip)
   - [AnimationPlayer](#animationplayer)
   - [SkinnedMeshInstance](#skinnedmeshinstance)
   - [BoneAttachment](#boneattachment)
   - [TagAttachment](#tagattachment)
5. [Morph Animation](#5-morph-animation)
   - [MorphAnimation](#morphanimation)
   - [MorphMeshInstance](#morphmeshinstance)
6. [VFX Nodes](#6-vfx-nodes)
   - [ParticleSystemNode](#particlesystemnode)
   - [GrassSystemNode](#grasssystemnode)
   - [DecalSystemNode](#decalsystemnode)
   - [RibbonTrailNode](#ribbontrailnode)
   - [LensFlareNode](#lensflarenode)
   - [WaterNode](#waternode)
   - [OceanNode](#oceannode)
   - [MirrorNode](#mirrornode)
7. [Terrain System](#7-terrain-system)
   - [TerrainNode](#terrainnode)
   - [InfiniteTerrainNode](#infiniteterrainnode)
   - [TerrainLodNode](#terrainlodnode)
   - [TerrainPagingNode](#terrainpagingnode)
   - [TiledTerrainNode](#tiledterrainnode)
8. [CSG & Volume](#8-csg--volume)
   - [CSG](#csg)
   - [VolumeSource](#volumesource)
   - [VolumeGridSource](#volumegridsource)
   - [VolumeNoise](#volumenoise)
   - [VolumeCSGSource](#volumecsgsource)
   - [BspInstance](#bspinstance)
9. [Behavior Controllers](#9-behavior-controllers)
10. [Math Library](#10-math-library)
11. [Utility Systems](#11-utility-systems)
    - [AssetManager](#assetmanager)
    - [Input](#input)
    - [Color](#color)
    - [Pixmap](#pixmap)
    - [Collision](#collision)
    - [Tree](#tree)
    - [IO](#io)
    - [Filesystem](#filesystem)
    - [ByteArray](#bytearray)

---

## Design Goals

- **Node-based hierarchy** — A single `Node` base class with ownership
  semantics. `add_child()` transfers ownership; `create_child<T>()` allocates
  in-place. The scene graph owns all nodes and resources.
- **Non-owning resource pointers** — Nodes reference meshes, textures, and
  materials via raw pointers. The `Scene` class owns and manages all resource
  lifetime.
- **Explicit lifecycle** — Three virtual hooks: `_ready()` (children-first),
  `_update(dt)` (self-first), `_release_gpu()` (children-first).
- **Dirty-flag transform caching** — `Node3D` tracks transform dirtiness and
  only recomputes world matrices when needed.
- **Data-driven rendering** — `RenderItem` structs carry all data needed for a
  draw call: VAO, index range, material, world matrix, optional skin palette.
- **Behavior controllers are transform-less** — Behaviors operate on the first
  `Node3D` ancestor via `get_target()`, making them reusable and composable.
- **Multiple terrain strategies** — Five distinct terrain implementations
  (static, infinite, GeoMipMap LOD, paging/streaming, tiled) for different
  use cases.
- **Dual CSG paradigms** — Polygon-based BSP-tree booleans (hard edges) and
  density-field volume operations (smooth blending).

---

## 1. Core Hierarchy

### Node

`#include <scene/Node.hpp>`

The base class for all scene graph elements. Provides ownership-based parent
/ child relationships and a virtual lifecycle.

**Node Type Enum (25 types):**

```cpp
enum class NodeType : int {
    Node, Empty, Node3D,
    MeshInstance, Light, Camera,
    SkinnedMesh, BoneAttachment, TagAttachment,
    ParticleSystem, GrassSystem, DecalSystem,
    RibbonTrail, LensFlare,
    Water, Ocean, Mirror,
    Terrain, InfiniteTerrain, TerrainLod, TerrainPaging, TiledTerrain,
    Volume, BspInstance,
    Behavior
};
```

**Key Methods:**

| Method | Description |
|--------|-------------|
| `add_child(Node* child)` | Transfer ownership of `child` to this node |
| `create_child<T>()` | Allocate and attach a `T` child (returns `T*`) |
| `get_parent()` | Parent node or `nullptr` |
| `get_children()` | Range of child nodes |
| `remove_from_parent()` | Detach from parent (ownership transferred to caller) |

**Virtual Lifecycle Hooks:**

| Hook | Order | Purpose |
|------|-------|---------|
| `_ready()` | Children first | One-time initialization after tree assembly |
| `_update(float dt)` | Self first | Per-frame logic |
| `_release_gpu()` | Children first | Free GPU resources before context destruction |

```cpp
auto* root = scn.root();
auto* child = root->create_child<scene::Node3D>();
child->set_name("Player");
```

---

### Node3D

`#include <scene/Node3D.hpp>`

Spatial node with position, rotation (quaternion), and scale. Inherits `Node`.
Uses dirty-flag caching for local and world matrices.

**Transform:**

```cpp
Vec3       position;      // local position
Quaternion rotation;      // local rotation
Vec3       scale = {1,1,1}; // local scale
```

**Movement:**

| Method | Description |
|--------|-------------|
| `move(Vec3 offset)` | Translate in local space |
| `move_global(Vec3 offset)` | Translate in world space |
| `advance(float dist)` | Move along forward axis (−Z) |
| `strafe(float dist)` | Move along right axis (+X) |
| `lift(float dist)` | Move along up axis (+Y) |

**Rotation:**

| Method | Description |
|--------|-------------|
| `set_euler(float yaw, float pitch, float roll)` | Set rotation from Euler angles (YXZ order) |
| `rotate(Vec3 euler)` | Add Euler rotation |
| `rotate_axis(float angle, Vec3 axis)` | Rotate around arbitrary axis |
| `rotate_global(float angle, Vec3 axis)` | Rotate in world space |
| `look_at(Vec3 target, Vec3 up)` | Orient toward target |

**Axes (read-only):**

| Method | Returns |
|--------|---------|
| `forward()` | −Z direction (local) |
| `right()` | +X direction (local) |
| `up()` | +Y direction (local) |
| `forward_global()` / `right_global()` / `up_global()` | World-space equivalents |

**Matrices:**

| Method | Description |
|--------|-------------|
| `get_local_matrix()` | Local transform (cached, recomputed on dirty) |
| `get_global_matrix()` | World transform (cached, recomputed on dirty) |
| `set_position(Vec3)` / `get_position()` | Position accessor |
| `set_rotation(Quaternion)` / `get_rotation()` | Rotation accessor |
| `set_scale(Vec3)` / `get_scale()` | Scale accessor |

```cpp
node->set_position({ 0, 5, -10 });
node->set_euler(90.0f, 0, 0); // yaw 90°
node->look_at({ 0, 0, 0 });
Vec3 fwd = node->forward();
```

---

### Scene

`#include <scene/Scene.hpp>`

Top-level container that owns the scene graph, meshes, materials, and cameras.

**RenderItem struct:**

```cpp
struct RenderItem {
    unsigned int vao;
    unsigned int first_index;
    unsigned int index_count;
    Material*    material;
    Mat4         world;
    const Mat4*  skin_palette; // nullptr for non-skinned
    int          skin_count;
};
```

**Key Methods:**

| Method | Description |
|--------|-------------|
| `root()` | Root `Node` of the scene graph |
| `ready()` | Call `_ready()` on entire tree (once) |
| `update(float dt)` | Call `_update(dt)` on entire tree (per frame) |
| `create_mesh()` | Allocate a scene-owned `Mesh` |
| `create_material()` | Allocate a scene-owned `Material` |
| `set_active_camera(Camera3D*)` | Set the primary camera |
| `get_active_camera()` | Active camera or `nullptr` |
| `find_camera()` | Find first `Camera3D` in tree |
| `collect_cameras(vector<Camera3D*>&)` | Collect all cameras |
| `collect(vector<RenderItem>&, const Frustum&)` | Gather visible items |
| `collect(vector<RenderItem>&, const Frustum&, SceneOctree*)` | With spatial index |
| `collect_bsp(vector<RenderItem>&)` | Gather BSP-rendered items |
| `release_gpu()` | Free all GPU resources |

```cpp
auto* cam = scn.root()->create_child<scene::Camera3D>();
cam->set_perspective(60.0f, 0.1f, 1000.0f);
scn.set_active_camera(cam);
```

---

### Camera3D

`#include <scene/Camera3D.hpp>`

Perspective/orthographic camera with frustum extraction and screen/ray
conversion.

**Key Methods:**

| Method | Description |
|--------|-------------|
| `set_perspective(float fov, float near, float far)` | Perspective projection |
| `set_orthographic(float size, float near, float far)` | Orthographic projection |
| `get_projection_matrix()` | Projection matrix |
| `get_view_matrix()` | View matrix (from Node3D global transform) |
| `get_frustum()` | Extract view frustum (6 planes) |
| `screen_to_ray(float px, float py, int w, int h)` | Screen pixel → world `Ray` |
| `world_to_screen(Vec3 world, int w, int h)` | World point → screen coords |

```cpp
cam->set_perspective(60.0f, 0.1f, 1000.0f);
coregl::Ray ray = cam->screen_to_ray(mouseX, mouseY, width, height);
```

---

### LightNode

`#include <scene/LightNode.hpp>`

Light source node. Three types: base (ambient/directional), point, spot.

**Light Hierarchy:**

```cpp
class LightNode     : public Node3D { ... }; // NT_LIGHT — directional/ambient
class PointLight    : public LightNode { float range; ... };
class SpotLight     : public LightNode { float cone_angle; ... }; // cone along −Z
```

**Properties:**

| Property | Type | Description |
|----------|------|-------------|
| `color` | `Vec3` | Light color (linear RGB) |
| `intensity` | `float` | Brightness multiplier |
| `cast_shadows` | `bool` | Whether this light casts shadows |

**Limits:**
- Max **4 point/spot lights** active per frame
- Max **2 shadow-casting lights** of each type (point/spot)
- Point lights use depth cubemaps; spot lights use 2D depth maps

---

## 2. Mesh & Material System

### Mesh

`#include <scene/Mesh.hpp>`

GPU mesh resource holding vertices, indices, and surfaces. A mesh can have
multiple surfaces (sub-meshes), each with its own material slot.

**Vertex Layout:**

```cpp
struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec4 tangent;   // .w = handedness for bitangent
    Vec2 uv;
};

struct Surface {
    unsigned int first_index;
    unsigned int index_count;
    int          material_slot;
    BoundingBox  bounds;
};
```

**Key Methods:**

| Method | Description |
|--------|-------------|
| `set_data(vertices, indices)` | Upload full mesh data |
| `add_surface(first_index, index_count, slot)` | Define a sub-mesh |
| `compute_normals()` | Recalculate vertex normals |
| `compute_tangents()` | Recalculate tangents + bitangent handedness |
| `compute_bounds()` | Calculate bounding box |
| `upload()` | Upload to GPU (GL_STATIC_DRAW) |
| `upload_dynamic()` | Upload with GL_DYNAMIC_DRAW for streaming |
| `update_vertices(offset, data)` | Partial vertex update (dynamic meshes) |
| `update_indices(offset, data)` | Partial index update (dynamic meshes) |
| `get_bounds()` | Bounding box |
| `get_surface_count()` | Number of surfaces |

```cpp
auto* mesh = scn.create_mesh();
std::vector<scene::MeshVertex> verts = { ... };
std::vector<unsigned int>       indices = { ... };
mesh->set_data(verts, indices);
mesh->add_surface(0, indices.size(), 0);
mesh->compute_normals();
mesh->compute_bounds();
mesh->upload();
```

---

### Material

`#include <scene/Material.hpp>`

Plain-data material descriptor. Not a class with virtuals — just a struct with
properties the renderer reads.

```cpp
struct Material {
    Vec3        base_color    = {1, 1, 1};
    Texture*    diffuse       = nullptr;
    Texture*    detail        = nullptr;
    float       detail_scale  = 40.0f;
    bool        double_sided  = false;
    bool        unlit         = false;
    bool        lightmapped   = false;
    bool        specular      = false;
    float       shininess     = 32.0f;
};
```

**Properties:**

| Property | Default | Description |
|----------|---------|-------------|
| `base_color` | `{1,1,1}` | Tint multiplied with diffuse texture |
| `diffuse` | `nullptr` | Primary albedo texture |
| `detail` | `nullptr` | Secondary detail texture (tiled) |
| `detail_scale` | `40.0f` | Detail texture UV repeat |
| `double_sided` | `false` | Disable backface culling |
| `unlit` | `false` | Skip lighting, render raw color |
| `lightmapped` | `false` | Use second UV set (uv2) for lightmap |
| `specular` | `false` | Enable specular highlights |
| `shininess` | `32.0f` | Specular exponent |

---

### MeshInstance

`#include <scene/MeshInstance.hpp>`

Scene graph node that renders a `Mesh`. Inherits `Node3D`.

**Key Methods:**

| Method | Description |
|--------|-------------|
| `set_mesh(Mesh* mesh)` | Set the mesh (non-owning pointer) |
| `set_material(Material* mat)` | Override material for all surfaces |
| `set_materials(vector<Material*>)` | Per-surface materials |
| `set_visible(bool)` | Toggle visibility |

```cpp
auto* inst = scn.root()->create_child<scene::MeshInstance>();
inst->set_mesh(cube_mesh);
inst->set_material(mat);
inst->set_position({ 0, 0, -5 });
```

---

### MeshLoader

`#include <scene/MeshLoader.hpp>`

Multi-format mesh loader. Uses **assimp offline** in the exporter tool to
convert source formats (FBX, OBJ, glTF, etc.) into a compact binary `.mesh`
format loaded at runtime.

**Runtime Binary Format:**

```
"MESH" magic header
├── Chunks: vertices, indices, surfaces, bounds
├── Optional: UV2 (lightmap), vertex colors
└── Optional: skeleton, bone weights, animation tracks
```

**Supported Source Formats (via exporter):**

| Format | Notes |
|--------|-------|
| glTF 2.0 | Full support including skinning and morph targets |
| FBX | Via assimp |
| OBJ | Via assimp |
| Blitz3D (.b3d) | Legacy runtime support |
| IQM | Inter-Quake Model |
| BSP | Quake III map format |
| MD2 / MD3 | Legacy Quake model formats |
| 3DS | Via assimp |

```cpp
// Runtime: load pre-converted binary mesh
Mesh* mesh = MeshLoader::load("assets/models/player.mesh");
```

---

### MeshPrimitives

`#include <scene/MeshPrimitives.hpp>`

Procedural geometry generators that fill a `Mesh` without any external file.

**Available Primitives:**

| Function | Description |
|----------|-------------|
| `cube(Mesh&, float size)` | Unit cube |
| `plane(Mesh&, float w, float h, int segX, int segY)` | Segmented plane |
| `hills_plane(Mesh&, float w, float h, int seg)` | Plane with sinusoidal hills |
| `heightfield(Mesh&, float* heights, int w, int h, float scale)` | Heightmap terrain |
| `sphere(Mesh&, float radius, int slices, int stacks)` | UV sphere |
| `cylinder(Mesh&, float radius, float height, int segments)` | Solid cylinder |
| `cone(Mesh&, float radius, float height, int segments)` | Solid cone |
| `capsule(Mesh&, float radius, float height, int segments)` | Capsule (cylinder + hemispheres) |

```cpp
scene::MeshPrimitives::sphere(*mesh, 1.0f, 32, 16);
scene::MeshPrimitives::capsule(*mesh, 0.5f, 2.0f, 16);
```

---

## 3. Rendering Pipeline

### SceneRenderer

`#include <scene/SceneRenderer.hpp>`

The main renderer. Walks the scene graph, collects visible render items, and
draws them with internal shaders. Supports skybox/skydome, post-processing
(godrays, SSAO), and cascaded shadow maps (CSM).

**Pipeline Overview:**

```
render(Scene&, w, h)
 │
 ├── 1. Collect RenderViews
 │      Main view (always)
 │      + special views from nodes:
 │        WaterNode/OceanNode → refraction + reflection views
 │        MirrorNode → reflection view
 │        PointLight/SpotLight (cast_shadows) → depth views
 │
 ├── 2. For each RenderView:
 │      a. Collect visible RenderItems (frustum + optional octree)
 │      b. CSM shadow pass (depth-only, if enabled)
 │      c. Render to target (FBO)
 │      d. Apply view clip plane (water/mirror)
 │
 ├── 3. Sky rendering (skybox cubemap or skydome)
 │
 ├── 4. Composite + post-processing
 │      Godrays, SSAO, tone mapping (exposure)
 │
 └── 5. Output to default framebuffer
```

**RenderView struct (internal):**

```cpp
struct RenderView {
    Mat4    view;
    Mat4    proj;
    FrameBuffer* target;    // nullptr = default framebuffer
    Vec4    clip_plane;     // for water/mirror (0,0,0,0 = none)
    bool    mirrored;       // flipped winding for reflections
    Vec3    cam_pos;
};
```

**Internal Shaders:**

| Shader | Purpose |
|--------|---------|
| `m_forward` | Lit forward rendering (blinn-phong + up to 4 point/spot lights) |
| `m_bsp` | Lightmapped rendering (uses uv2) |
| `m_depth` | Depth-only pass for CSM shadow maps |

**Key Methods:**

| Method | Description |
|--------|-------------|
| `init()` | Initialize shaders and internal resources |
| `release()` | Free all GPU resources |
| `render(Scene&, int w, int h)` | Render the scene to default framebuffer |

**Sky:**

| Method | Description |
|--------|-------------|
| `set_skybox(Texture* cubemap)` | Set skybox cubemap |
| `set_skydome(Texture* sky, Texture* cloud)` | Set skydome textures |

**Post-Processing:**

| Method | Description |
|--------|-------------|
| `enable_post(bool godrays, bool ssao)` | Enable post-processing effects |
| `set_exposure(float)` | Tone mapping exposure |
| `set_ssao_params(int samples, float radius, float bias)` | SSAO tuning |

**Shadows (CSM):**

| Method | Description |
|--------|-------------|
| `enable_shadows(int cascades = 4, int resolution = 2048, float distance = 200)` | Enable CSM |
| `set_shadow_bias(float)` | Adjust shadow depth bias |

**Spatial Index:**

| Method | Description |
|--------|-------------|
| `build_spatial_index(Scene&)` | Build octree from scene |
| `set_use_spatial_index(bool)` | Toggle octree-accelerated culling |

```cpp
renderer.init();
renderer.set_skybox(cubemap_tex);
renderer.enable_shadows(4, 2048, 200.0f);
renderer.enable_post(true, true); // godrays + SSAO
renderer.build_spatial_index(scn);

// Main loop
while (running) {
    scn.update(dt);
    renderer.render(scn, width, height);
}
```

---

### SceneOctree

`#include <scene/SceneOctree.hpp>`

Octree spatial index for accelerating frustum culling. Indexes at **surface
granularity** — each octree entry references a specific mesh surface, not just
a node.

```cpp
struct SceneOctreeEntry {
    MeshInstance* instance;
    int           surfaceIndex;
    BoundingBox   worldBounds;
};
```

**Key Methods:**

| Method | Description |
|--------|-------------|
| `build(Scene&)` | Populate from all `MeshInstance` nodes |
| `query(Frustum&, vector<SceneOctreeEntry>&)` | Return visible surfaces |
| `clear()` | Remove all entries |

---

## 4. Skeletal Animation

### SkinnedMesh

`#include <scene/SkinnedMesh.hpp>`

Shared, immutable skinned model resource. Contains the base mesh plus
per-vertex bone weights and a `Skeleton`.

**Vertex Weights:**

```cpp
struct VertexWeights {
    int   bone[4];    // bone indices
    float weight[4];  // corresponding weights (sum to 1.0)
};
```

The skinned mesh is a **shared resource** — multiple `SkinnedMeshInstance`
nodes can reference it simultaneously, each with independent animation state.

---

### Skeleton

`#include <scene/Skeleton.hpp>`

Bone hierarchy using a flat array with parent indices for cache-friendly
evaluation.

```cpp
struct Bone {
    std::string name;
    int         parent;     // -1 for root
    Mat4        bindLocal;  // local transform in bind pose
    Mat4        inverseBind; // to convert mesh-space to bone-space
};
```

**Key Methods:**

| Method | Description |
|--------|-------------|
| `evaluate(const Mat4* localTransforms, Mat4* outPalette)` | Compute final skinning palette via topological sort |

Evaluation is done in **topological order** (parents before children) so that
each bone's world transform can be computed from its parent's already-computed
transform.

---

### AnimationClip

`#include <scene/AnimationClip.hpp>`

A single animation clip containing bone tracks. Each track animates one bone's
local transform (position, rotation, scale).

```cpp
struct BoneTrack {
    int boneIndex;
    // Keyframe arrays (parallel, sorted by time)
    std::vector<Vec3>       positions;
    std::vector<Quaternion> rotations;
    std::vector<Vec3>       scales;
    std::vector<float>      times;

    void sample(float time, Vec3& pos, Quaternion& rot, Vec3& scl) const;
};
```

Bones without tracks retain their existing pose — only animated bones are
modified during sampling.

---

### AnimationPlayer

`#include <scene/AnimationPlayer.hpp>`

Ogre-style layered animation blending system. Supports multiple simultaneously
playing layers, crossfades, and one-shot animations with automatic return.

```cpp
struct AnimationLayer {
    AnimationClip* clip       = nullptr;
    float          weight     = 1.0f;
    float          time       = 0.0f;
    bool           looping    = true;
    bool           active     = false;
    float          fade_in    = 0.0f;   // crossfade in duration
    float          fade_out   = 0.0f;   // crossfade out duration
    bool           one_shot   = false;  // auto-deactivate when finished
};
```

**Key Methods:**

| Method | Description |
|--------|-------------|
| `play(const std::string& name, int layer, float fadeTime)` | Start playing a clip on a layer |
| `stop(int layer)` | Stop animation on a layer |
| `crossfade(int fromLayer, int toLayer, float duration)` | Smooth transition |
| `update(float dt)` | Advance all active layers and blend |
| `get_palette(Skeleton&, Mat4* outPalette)` | Evaluate blended transforms into skin palette |

```cpp
player.play("idle", 0, 0.0f);    // base layer: idle loop
player.play("wave", 1, 0.3f);    // overlay: wave with 0.3s fade-in
```

---

### SkinnedMeshInstance

`#include <scene/SkinnedMeshInstance.hpp>`

Scene graph node that renders a `SkinnedMesh` with its own animation state.
Inherits `Node3D` (`NT_SKINNEDMESH`).

**Key Methods:**

| Method | Description |
|--------|-------------|
| `set_skinned_mesh(SkinnedMesh* mesh)` | Set the shared skinned mesh resource |
| `get_player()` | Access the per-instance `AnimationPlayer` |
| `play(const std::string& name, ...)` | Convenience: play animation on base layer |

The `_update(dt)` method advances the `AnimationPlayer` and evaluates the
skeleton **in the same frame** — no deferred evaluation.

---

### BoneAttachment

`#include <scene/BoneAttachment.hpp>`

Attaches child nodes to a specific bone of a parent `SkinnedMeshInstance`.
Inherits `Node3D` (`NT_BONEATTACHMENT`).

**Design:** Only one node per attachment is created for each attachment
actually used — no wasted overhead for unused bones.

```cpp
auto* hand = skinnedNode->create_child<scene::BoneAttachment>();
hand->set_bone("Hand_R");
auto* sword = hand->create_child<scene::MeshInstance>();
sword->set_mesh(swordMesh);
```

---

### TagAttachment

`#include <scene/TagAttachment.hpp>`

Attaches child nodes to MD3 model tags (named anchor points in multi-part
character rigs). Inherits `Node3D` (`NT_TAGATTACHMENT`).

Used for assembling multi-part characters (e.g., head + torso + legs from
separate MD3 files connected via named tags).

```cpp
auto* head_tag = torsoNode->create_child<scene::TagAttachment>();
head_tag->set_tag("tag_head");
head_tag->attach(headMeshInstance);
```

---

## 5. Morph Animation

### MorphAnimation

`#include <scene/MorphAnimation.hpp>`

Vertex morph (blend shape) animation system. Animates between multiple target
meshes by blending vertex positions.

**Key Types:**

```cpp
struct MorphKeyframes {
    std::vector<float>           times;
    std::vector<std::vector<Vec3>> targets; // per-keyframe vertex deltas
};

struct MorphClip {
    std::string                  name;
    std::vector<MorphKeyframes>  keyframes;
    float                        duration;
};

class MorphAnimator {
    // Crossfade between two PlayStates
    struct PlayState { MorphClip* clip; float time; float weight; };
    PlayState m_states[2];
    float     m_crossfade = 0.0f;
    void update(float dt);
    void blend(std::vector<Vec3>& outVertices) const;
};
```

**Features:**
- Crossfade between two animation states
- Weight-based vertex blending
- Multiple morph targets per keyframe

---

### MorphMeshInstance

`#include <scene/MorphMeshInstance.hpp>`

Scene graph node that renders a morph-animated mesh. Inherits `MeshInstance`
(is-a, not has-a relationship).

**Key Methods:**

| Method | Description |
|--------|-------------|
| `set_morph_mesh(Mesh* base, MorphClip* clip)` | Set base mesh and animation |
| `play(const std::string& name, float fadeTime)` | Start morph animation |
| `update(float dt)` | Advance animator and update vertex buffer |

Uses `upload_dynamic()` on the mesh for efficient per-frame vertex updates.

---

## 6. VFX Nodes

### ParticleSystemNode

`#include <scene/ParticleSystemNode.hpp>`

GPU-accelerated particle system with configurable emitters, affectors, and
blend modes. Uses dynamic VBO rebuild per frame.

**Configuration:**

```cpp
enum class ParticleBlendMode { Opaque, Additive, AlphaBlend };
enum class EmissionMode     { Continuous, Burst };
enum class EmitterShape     { Point, Box, Sphere, Cone };
```

**Particle struct:**

```cpp
struct Particle {
    Vec3  position;
    Vec3  velocity;
    Vec4  color;
    Vec2  size;
    float life;
    float max_life;
    float rotation;
};
```

**Affectors (7 built-in):**

| Affector | Effect |
|----------|--------|
| `GravityAffector` | Constant acceleration (e.g., gravity) |
| `DragAffector` | Velocity damping over time |
| `ColorAffector` | Interpolate color across lifetime |
| `SizeAffector` | Scale particles over lifetime |
| `VortexAffector` | Rotational force around an axis |
| `AttractorAffector` | Pull toward a point |
| `TurbulenceAffector` | Procedural noise-based jitter |

```cpp
auto* ps = scn.root()->create_child<scene::ParticleSystemNode>();
ps->set_emission_rate(100.0f);     // particles per second
ps->set_emitter_shape(scene::EmitterShape::Sphere);
ps->set_blend_mode(scene::ParticleBlendMode::Additive);
ps->set_particle_lifetime(2.0f, 4.0f);
ps->add_affector<scene::GravityAffector>({ 0, -9.8f, 0 });
```

---

### GrassSystemNode

`#include <scene/GrassSystemNode.hpp>`

Static grass rendering system. Unlike particles, the grass mesh is built once
and rendered as a static mesh (no per-frame rebuild). Includes wind sway via
vertex shader.

**Grass Types:**

```cpp
enum class GrassType { Single, Cross, TriCross };
```

| Type | Description |
|------|-------------|
| `Single` | Single billboard quad |
| `Cross` | Two crossed quads (X pattern) |
| `TriCross` | Three quads at 60° intervals |

**Key Methods:**

| Method | Description |
|--------|-------------|
| `addClump(Vec3 pos, float height)` | Add a grass clump |
| `fillArea(Vec2 center, float radius, int count)` | Scatter grass randomly in area |
| `set_wind_strength(float)` | Wind animation strength |
| `set_wind_direction(Vec2)` | Wind direction (XZ) |

---

### DecalSystemNode

`#include <scene/DecalSystemNode.hpp>`

Surface-projected decal system. Renders quads projected onto geometry. Uses
dynamic VBO rebuild per frame and **reuses the particle shader pass** for
efficient rendering.

```cpp
auto* decals = scn.root()->create_child<scene::DecalSystemNode>();
decals->add({ x, y, z }, 1.0f, bulletHoleTex);
```

---

### RibbonTrailNode

`#include <scene/RibbonTrailNode.hpp>`

Camera-facing ribbon trail system with Catmull-Rom smoothing. Supports up to
4 simultaneous chains and a blade mode for flat ribbon strips. Uses dynamic
VBO per frame and **reuses the particle shader**.

**Key Features:**

| Feature | Description |
|----------|-------------|
| Camera-facing | Always faces camera for consistent thickness |
| Catmull-Rom | Smooth interpolation between trail points |
| Blade mode | Flat ribbon (like a sword trail) |
| Multi-chain | `MAX_CHAINS = 4` independent trails |

```cpp
auto* trail = scn.root()->create_child<scene::RibbonTrailNode>();
trail->set_max_points(0, 50);     // chain 0, 50 points
trail->set_width(0, 0.5f);       // chain 0 width
trail->add_point(0, node->get_position());
```

---

### LensFlareNode

`#include <scene/LensFlareNode.hpp>`

Cinematic lens flare using **hardware occlusion queries** (`GL_ANY_SAMPLES_PASSED`).
Renders a series of flare sprites whose visibility and intensity are determined
by whether the light source is occluded by geometry. Uses dynamic VBO per frame.

```cpp
auto* flare = scn.root()->create_child<scene::LensFlareNode>();
flare->set_position({ 100, 200, -500 });
flare->set_texture(flareTex);
flare->set_intensity(1.0f);
```

---

### WaterNode

`#include <scene/WaterNode.hpp>`

Planar water surface with refraction and reflection. The renderer generates
two additional `RenderView`s (refraction + reflection) for each visible
water node. Reflective face is **+Y** (up).

**Key Methods:**

| Method | Description |
|--------|-------------|
| `set_size(float w, float h)` | Set water plane dimensions |
| `set_surface_height(float y)` | Set water surface Y coordinate |

**Properties:**

| Property | Default | Description |
|----------|---------|-------------|
| `water_color` | `{0.1, 0.3, 0.5}` | Deep water tint |
| `wave_speed` | `0.1` | Wave animation speed |
| `distortion` | `0.02` | Refraction distortion amount |
| `wave_tiling` | `1.0` | Normal map UV tiling |
| `color_mix` | `0.5` | Blend between reflection and water color |

---

### OceanNode

`#include <scene/OceanNode.hpp>`

Ocean water with Gerstner wave simulation and foam. Inherits `WaterNode`.

**Gerstner Waves:**

```cpp
Vec4 waves[4]; // each: direction.xy, steepness, wavelength
```

**Key Properties:**

| Property | Default | Description |
|----------|---------|-------------|
| `waves[4]` | preset | 4 Gerstner wave parameters |
| `foam_texture` | `nullptr` | Foam overlay texture |
| `bump_texture` | `nullptr` | Normal/height map for surface detail |
| `grid_resolution` | `200` | Vertex grid density |

---

### MirrorNode

`#include <scene/MirrorNode.hpp>`

Flat mirror surface with Fresnel reflectivity. Reflection-only (no refraction).
The renderer generates one additional reflection `RenderView` per visible
mirror. Reflective face is **+Y**.

**Key Properties:**

| Property | Default | Description |
|----------|---------|-------------|
| `tint` | `{0.8, 0.8, 0.8}` | Mirror surface tint |
| `reflectivity` | `0.15` | Fresnel reflectivity at normal incidence |

```cpp
auto* mirror = scn.root()->create_child<scene::MirrorNode>();
mirror->set_size(4.0f, 4.0f);
mirror->set_position({ 0, 2, 0 });
```

---

## 7. Terrain System

The engine provides five distinct terrain implementations, each suited for
different use cases:

| Node | Strategy | Best For |
|------|----------|----------|
| `TerrainNode` | Static block mesh | Small, fixed heightmaps |
| `InfiniteTerrainNode` | LRU cache + pre-built LOD | Endless procedural terrain |
| `TerrainLodNode` | GeoMipMap | Medium terrain with editing |
| `TerrainPagingNode` | Grid2D page streaming | Large, realistic terrain |
| `TiledTerrainNode` | u8 tilemap + atlas | Retro/tilemap-style terrain |

---

### TerrainNode

`#include <scene/TerrainNode.hpp>`

Block-based heightmap terrain extending `MeshInstance`. Fixed mesh with
configurable vertex density.

**Constants:**

```cpp
static const int BLOCK_VERTS = 33; // vertices per block edge (32×32 cells)
```

**Key Methods:**

| Method | Description |
|--------|-------------|
| `load_heightmap(const char* path)` | Load heightmap from image file |
| `build()` | Generate mesh from loaded heightmap |
| `height_at(float x, float z)` | Sample height at world coordinates |
| `normal_at(float x, float z)` | Get surface normal at world coordinates |
| `raycast(Ray ray, Vec3& hit)` | Ray-terrain intersection test |

```cpp
auto* terrain = scn.root()->create_child<scene::TerrainNode>();
terrain->load_heightmap("assets/terrain/heightmap.png");
terrain->build();
terrain->upload();
```

---

### InfiniteTerrainNode

`#include <scene/InfiniteTerrainNode.hpp>`

Endless procedural terrain with LRU patch caching. Extends `Node3D`.

**Design:**
- Camera-position-driven patch ring: patches are created/destroyed as the
  camera moves.
- Each patch has **pre-built LOD mesh variants** (no dynamic IB generation).
- **Skirts** between patches prevent T-junction cracks.
- Height function is user-supplied via callback.

```cpp
float my_height_func(float x, float z) {
    return sin(x * 0.1f) * cos(z * 0.1f) * 10.0f;
}

auto* terrain = scn.root()->create_child<scene::InfiniteTerrainNode>();
terrain->set_height_function(my_height_func);
terrain->set_view_distance(500.0f);
```

**Constants:**

```cpp
static const int MAX_CACHED = 512; // max patches in LRU cache
static const int MAX_LOD    = 4;   // LOD levels per patch
```

**Patch key encoding:**

```cpp
int64_t key = ((int64_t)px << 32) | (uint32_t)pz;
```

---

### TerrainLodNode

`#include <scene/TerrainLodNode.hpp>`

GeoMipMap terrain with dynamic LOD. Each patch independently selects its LOD
based on distance to camera.

**Design:**
- Single VBO per patch + **dynamic IBO** rebuilt when LOD changes.
- Patches are **crack-aware** — they stitch edges with neighbors of different
  LOD to prevent gaps.
- Supports **real-time editing** with `modify_height()` and `smooth_area()`,
  followed by `rebake()` to update GPU buffers.

**Key Methods:**

| Method | Description |
|--------|-------------|
| `set_heightmap(float* data, int w, int h)` | Set heightmap data |
| `build(int patchSize)` | Build patch grid |
| `set_lod_threshold(float dist)` | Camera distance threshold for LOD switch |
| `modify_height(float x, float z, float delta)` | Edit terrain height |
| `smooth_area(float x, float z, float radius)` | Smooth terrain area |
| `rebake()` | Rebuild GPU buffers after editing |
| `height_at(float x, float z)` | Sample height |
| `raycast(Ray, Vec3& hit)` | Ray intersection |

```cpp
auto* terrain = scn.root()->create_child<scene::TerrainLodNode>();
terrain->set_heightmap(heights, 512, 512);
terrain->build(64);         // 64×64 patches
terrain->set_lod_threshold(80.0f);

// Real-time editing
terrain->modify_height(px, pz, +5.0f);
terrain->rebake();
```

---

### TerrainPagingNode

`#include <scene/TerrainPagingNode.hpp>`

Large-scale terrain with **Ogre Grid2D page strategy** streaming. The most
advanced terrain implementation.

**Design:**
- Pages are loaded/unloaded based on camera position using a 2D grid strategy.
- LOD selection per-page using **W. de Boer (2000)** distance-based algorithm.
- **Texture splatting** with up to 5 layers, each with its own texture and a
  user-supplied blend function (`BlendFn`).
- **Terrain-specific distance fog** for smooth page pop-in.
- **Ray picking** via coarse march + bisection refinement.
- **Persistent height editing** — brush edits survive page unload/reload via
  an edit map keyed by page coordinates.

**Splat Layers:**

```cpp
struct Layer {
    Texture* texture;
    float    tiling;
};
Layer m_layers[5];
int   m_layerCount = 0;

using BlendFn = std::function<float(float x, float z, int layer)>;
BlendFn m_blendFn;
```

**Key Methods:**

| Method | Description |
|--------|-------------|
| `set_height_function(HeightFn fn)` | Set heightmap callback |
| `set_view_distance(float dist)` | Page loading range |
| `add_layer(Texture* tex, float tiling)` | Add splat layer (max 5) |
| `set_blend_function(BlendFn fn)` | Set layer blending callback |
| `set_fog(float start, float end, Vec3 color)` | Configure distance fog |
| `height_at(float x, float z)` | Sample height at world coordinates |
| `raycast(Ray, Vec3& hit)` | Ray intersection (coarse + bisection) |
| `modify_height(float x, float z, float delta)` | Edit terrain height (persistent) |
| `update(float dt)` | Process build queue, load/unload pages |
| `render_pages(...)` | Render splat pass (renderer-facing) |
| `render_pages_depth(...)` | Render depth-only CSM pass (renderer-facing) |

**Fog Defaults:**

```cpp
bool fogEnabled = true;
Vec3 fogColor   = {0.5, 0.6, 0.7};
float fogStart  = 400.0f;
float fogEnd    = 1600.0f;
```

**Edit Persistence:**

```cpp
std::unordered_map<uint64_t, std::vector<float>> m_edits;
// Key: encoded page coordinates. Value: height deltas.
// Edits are reapplied when a page is reloaded.
```

```cpp
auto* terrain = scn.root()->create_child<scene::TerrainPagingNode>();
terrain->set_height_function(my_terrain_height);
terrain->set_view_distance(2000.0f);
terrain->add_layer(grassTex,  40.0f);
terrain->add_layer(rockTex,   20.0f);
terrain->add_layer(snowTex,   30.0f);
terrain->set_blend_function(my_blend);
terrain->set_fog(400.0f, 1600.0f, {0.5f, 0.6f, 0.7f});
```

---

### TiledTerrainNode

`#include <scene/TiledTerrainNode.hpp>`

Tile-based terrain using a u8 tilemap index into a texture atlas. Generates
quads with UV coordinates pointing into the atlas.

**Constructor:**

```cpp
TiledTerrainNode(int tilesInSide = 8, float patchLen = 1.0f,
                 int tilesPerPatch = 1, uint8_t defaultTile = 0);
```

| Parameter | Default | Description |
|-----------|---------|-------------|
| `tilesInSide` | 8 | Tiles per patch edge |
| `patchLen` | 1.0 | World-space length of each patch |
| `tilesPerPatch` | 1 | Atlas tiles per terrain tile |
| `defaultTile` | 0 | Default tile index |

**Design:**
- u8 tilemap → atlas-UV quads
- One surface per patch for efficient frustum culling
- Simple and fast for retro/grid-based terrain

```cpp
auto* terrain = scn.root()->create_child<scene::TiledTerrainNode>(16, 4.0f);
terrain->set_tilemap(tiles, 64, 64);
terrain->set_atlas(atlasTex, 4, 4); // 4×4 tiles in atlas
```

---

## 8. CSG & Volume

### CSG

`#include <scene/CSG.hpp>`

Constructive Solid Geometry using BSP-tree boolean operations. Produces
polygon-based meshes with **hard edges** at intersection boundaries.

```cpp
namespace CSG {
    enum class Operation { Union, Difference, Intersection };

    // Boolean operations on polygon meshes
    Mesh* boolean(const Mesh* a, const Mesh* b, Operation op);
}
```

**Operations:**

| Operation | Description |
|-----------|-------------|
| `Union` | A ∪ B (merge) |
| `Difference` | A − B (subtract B from A) |
| `Intersection` | A ∩ B (overlap only) |

```cpp
auto* result = CSG::boolean(boxMesh, sphereMesh, CSG::Operation::Difference);
// Creates a box with a spherical hole cut out
```

---

### VolumeSource

`#include <scene/VolumeSource.hpp>`

Abstract interface for density-field-based volume meshing (marching cubes).
**Convention: positive density = inside the surface.**

```cpp
namespace volume {
    class Source {
    public:
        virtual float density(float x, float y, float z) const = 0;
        virtual BoundingBox bounds() const = 0;
        float rayMarch(Ray ray, float maxDist, float step) const;
    };
}
```

**Marching Cubes Pipeline:**

1. Sample density at grid points within `bounds()`.
2. For each cell, determine which corners are inside/outside.
3. Look up triangulation table for the cell.
4. Interpolate edge crossings to place vertices on the iso-surface.

This produces **smooth blending** at intersection boundaries, unlike CSG
polygon operations.

---

### VolumeGridSource

`#include <scene/VolumeGridSource.hpp>`

Grid-based volume source with **trilinear interpolation** between sample
points. Loads from a binary `"VGRD"` format.

```cpp
class GridSource : public Source {
    float* m_data;
    int    m_w, m_h, m_d;
    // Trilinear interpolation between 8 nearest grid points
    float density(float x, float y, float z) const override;
};
```

**Binary Format:**

```
"VGRD" magic
int width, height, depth
float data[width * height * depth]
```

---

### VolumeNoise

`#include <scene/VolumeNoise.hpp>`

3D Simplex noise generator for procedural volume sources. Returns values in
`[-1, 1]`.

```cpp
class SimplexNoise {
public:
    SimplexNoise(unsigned int seed = 0);
    float noise(float x, float y, float z) const; // [-1, 1]
    float fbm(float x, float y, float z, int octaves, float persistence) const;
};
```

The `fbm()` method layers multiple octaves of noise for natural-looking
terrain and cloud patterns.

---

### VolumeCSGSource

`#include <scene/VolumeCSGSource.hpp>`

Volume-based CSG primitives and operations. These compose density fields for
**smooth-blended** boolean results.

**Primitive Sources:**

| Class | Description |
|-------|-------------|
| `CSGSphereSource` | Spherical density field |
| `CSGCubeSource` | Axis-aligned box density field |
| `CSGPlaneSource` | Half-space (everything below Y=0) |

**Composition:**

| Class | Description |
|-------|-------------|
| `CSGOperationSource` | Binary op (union/diff/intersect) on two sources |
| `CSGUnarySource` | Unary transform (e.g., smoothing, displacement) |

```cpp
using namespace volume;

CSGSphereSource rock({ 0, 0, 0 }, 5.0f);
CSGSphereSource cave({ 0, 1, 0 }, 2.0f);
CSGOperationSource terrain(&rock, &cave, CSG::Operation::Difference);

// Mesh the result
MarchingCubes mesher;
mesher.mesh(terrain, outputMesh);
```

---

### BspInstance

`#include <scene/BspInstance.hpp>`

Quake III BSP level instance. Inherits `Node3D` (`NT_BSPINSTANCE`). Loaded
from `.bsp` files, rendered with the dedicated BSP shader that uses the
second UV channel (uv2) for precomputed lightmaps.

**Key Methods:**

| Method | Description |
|--------|-------------|
| `load(const char* bspPath)` | Load BSP file |
| `set_lightmap(Texture* lm)` | Override lightmap atlas |
| `get_faces()` | Access BSP face data |

The renderer automatically routes `BspInstance` nodes to the `m_bsp` shader
instead of the standard `m_forward` shader.

---

## 9. Behavior Controllers

`#include <scene/Behavior.hpp>`

Behavior controllers are nodes that manipulate a target `Node3D`. They are
**transform-less** — they have no position, rotation, or scale of their own.
Instead, they operate on their first `Node3D` ancestor via `get_target()`.

```cpp
class Behavior : public Node {
public:
    bool enabled = true;
    Node3D* get_target();  // first Node3D ancestor
    virtual void on_process(Node3D& target, float dt) = 0;
    void _update(float dt) final override; // calls on_process if enabled
};
```

**Available Behaviors:**

### FreeFlyBehavior

`#include <scene/FreeFlyBehavior.hpp>`

Editor-style free-fly camera controller.

| Input | Action |
|-------|--------|
| W/A/S/D | Move (forward/left/back/right) |
| Q / E | Move down / up |
| Shift | Sprint |
| Mouse wheel | Adjust move speed |
| Right-click + drag | Look around |

**Defaults:** `move_speed = 8.0`, `sprint_factor = 3.0`, `sensitivity = 0.15`

```cpp
cam->create_child<scene::FreeFlyBehavior>();
```

### OrbitBehavior

`#include <scene/OrbitBehavior.hpp>`

Model-viewer orbit camera.

| Input | Action |
|-------|--------|
| Right-click + drag | Orbit (yaw/pitch) |
| Mouse wheel | Zoom in/out |

**Defaults:** `distance = 10.0`, `sensitivity = 0.25`, `zoom_factor = 1.5`,
limits `1.0`–`500.0`

### CharacterBehavior

`#include <scene/CharacterBehavior.hpp>`

FPS/TPS character controller with gravity, jumping, and optional collision.

| Input | Action |
|-------|--------|
| W/S | Walk forward/backward |
| A/D | Turn left/right |
| Mouse | Look (if mouse-look enabled) |
| Space | Jump |

**Parameters:**

| Parameter | Default | Description |
|-----------|---------|-------------|
| `radius` | `{0.4, 0.9, 0.4}` | Ellipsoid collision radius (X, Y, Z) |
| `eyeOffset` | `1.7` | Camera height above character origin |
| `forwardSpeed` | `6.0` | Walk speed (units/sec) |
| `gravity` | `20.0` | Gravity acceleration |
| `jumpSpeed` | `7.0` | Initial jump velocity |
| `boom` | `0.0` | Camera distance (0 = 1st person, >0 = 3rd person) |

When a `CollisionSystem*` is set, the character uses **Fauerby ellipsoid
sliding** collision response.

### FollowBehavior

`#include <scene/FollowBehavior.hpp>`

Smooth chase camera that follows a target `Node3D` at a world-space offset.

**Defaults:** `offset = {0, 4, 10}`, `smooth = 6.0`, `lookAt = true`

```cpp
follow->set_target(playerNode);
follow->offset = { 0, 5, 8 };
follow->smooth = 4.0f;
```

### MayaBehavior

`#include <scene/MayaBehavior.hpp>`

Maya/Autodesk-style navigation controller.

| Input | Action |
|-------|--------|
| Alt + LMB | Orbit (tumble) |
| Alt + MMB | Pan |
| Alt + RMB / Wheel | Zoom (dolly) |

**Defaults:** `distance = 12.0`, limits `0.5`–`1000.0`, `pitch = 20°`

### OscillateBehavior

`#include <scene/OscillateBehavior.hpp>`

Sinusoidal oscillation along an axis.

**Defaults:** `axis = {0, 1, 0}` (Y), `amplitude = 1.0`, `frequency = 0.5 Hz`

```cpp
auto* osc = platform->create_child<scene::OscillateBehavior>();
osc->axis = { 0, 1, 0 };
osc->amplitude = 2.0f;
osc->frequency = 1.0f;
```

### RotatorBehavior

`#include <scene/RotatorBehavior.hpp>`

Continuous rotation around an axis.

**Defaults:** `axis = {0, 1, 0}` (Y), `speed = 45.0°/sec`

```cpp
auto* rot = fan->create_child<scene::RotatorBehavior>();
rot->axis = { 0, 1, 0 };
rot->speed = 180.0f; // 180°/sec = half revolution per second
```

---

## 10. Math Library

`#include <scene/Math.hpp>`

A complete 3D math library with no external dependencies. All types are
plain structs with operator overloads.

### Types

| Type | Description |
|------|-------------|
| `Vec2` | 2D vector (x, y) |
| `Vec3` | 3D vector (x, y, z) |
| `Vec4` | 4D vector (x, y, z, w) |
| `Quaternion` | Unit quaternion (x, y, z, w) |
| `Mat4` | 4×4 column-major matrix |
| `Mat3` | 3×3 column-major matrix |
| `Ray` | Origin + direction |
| `Plane3D` | Plane equation (normal + d) |
| `BoundingBox` | Axis-aligned bounding box (min + max) |
| `Triangle` | 3 vertices |
| `Frustum` | 6 clipping planes |

### Common Operations

**Vector operations:**

```cpp
Vec3 a = {1, 2, 3}, b = {4, 5, 6};
Vec3 c = a + b;              // {5, 7, 9}
float d = dot(a, b);         // dot product
Vec3 cr = cross(a, b);       // cross product
float len = length(a);       // magnitude
Vec3 n = normalize(a);       // unit vector
Vec3 l = lerp(a, b, 0.5f);   // linear interpolation
```

**Quaternion:**

```cpp
Quaternion q = Quaternion::from_euler(yaw, pitch, roll);
Quaternion r = Quaternion::from_axis_angle(axis, angle);
Mat4 m = q.to_matrix();
Quaternion s = slerp(q1, q2, 0.5f); // spherical lerp
```

**Matrix:**

```cpp
Mat4 m = Mat4::identity();
Mat4 t = Mat4::translation({1, 2, 3});
Mat4 r = Mat4::rotation_y(90.0f);
Mat4 s = Mat4::scaling({2, 2, 2});
Mat4 comb = t * r * s;       // transform composition
Vec3 tp = comb.transform_point({1, 0, 0});
Vec3 td = comb.transform_direction({0, 0, -1});
Mat4 inv = comb.inverse();
```

**Geometric Types:**

```cpp
// Ray
Ray ray = { origin, normalize(direction) };
float t;
bool hit = ray.intersect_box(box, t);
bool hit2 = ray.intersect_triangle(v0, v1, v2, t);
bool hit3 = ray.intersect_plane(plane, t);

// BoundingBox
BoundingBox box = { {0,0,0}, {1,1,1} };
box.expand(point);
box.merge(otherBox);
bool contains = box.contains(point);
bool intersects = box.intersects(otherBox);

// Frustum (6-plane clipping volume)
Frustum f = Frustum::from_view_proj(viewProj);
int result = f.intersect_box(box); // Inside, Outside, Intersect
```

---

## 11. Utility Systems

### AssetManager

`#include <scene/AssetManager.hpp>`

Singleton resource manager using the **PIMPL** pattern. Loads and caches
textures, shaders, meshes, skinned meshes, and volume data.

**Supported Asset Types:**

| Method | Description |
|--------|-------------|
| `load_texture(const char* path)` | Load and cache an image texture |
| `load_shader(const char* vs, const char* fs)` | Compile and cache a shader |
| `load_mesh(const char* path)` | Load a `.mesh` binary mesh |
| `load_skinned(const char* path)` | Load a skinned mesh + skeleton + animations |
| `load_cubemap(const char* paths[6])` | Load 6-face cubemap |

The PIMPL pattern hides the implementation details and keeps the header
clean. Legacy formats (BSP, MD3, B3D) are loaded through specialized loaders.

```cpp
auto& am = scene::AssetManager::instance();
auto* tex = am.load_texture("assets/textures/stone.png");
auto* mesh = am.load_mesh("assets/models/rock.mesh");
```

---

### Input

`#include <scene/Input.hpp>`

Polled input system (raylib-style). The application pumps events and feeds
them to `Input`, then game logic queries state.

**Input Categories:**

| Category | Values |
|----------|--------|
| `MouseButton` | Left, Right, Middle, etc. |
| `KeyCode` | A–Z, 0–9, F1–F12, arrows, modifiers, etc. |
| `GamepadButton` | 18 buttons (A, B, X, Y, D-pad, bumpers, sticks, etc.) |
| `GamepadAxis` | 6 axes (LeftX, LeftY, RightX, RightY, LTrigger, RTrigger) |

**Key Methods (all static):**

| Method | Description |
|--------|-------------|
| `is_key_down(KeyCode)` | Is key currently held? |
| `is_key_pressed(KeyCode)` | Was key pressed this frame? |
| `is_key_released(KeyCode)` | Was key released this frame? |
| `is_mouse_down(MouseButton)` | Is mouse button held? |
| `is_mouse_pressed(MouseButton)` | Was mouse pressed this frame? |
| `get_mouse_x()` / `get_mouse_y()` | Mouse position |
| `get_mouse_delta()` | Mouse movement since last frame |
| `get_mouse_wheel()` | Scroll wheel delta |
| `is_gamepad_down(int pad, GamepadButton)` | Gamepad button held? |
| `get_gamepad_axis(int pad, GamepadAxis)` | Gamepad axis value `[-1, 1]` |
| `end_frame()` | Reset per-frame state (call after update) |

```cpp
if (scene::Input::is_key_down(scene::KeyCode::W)) {
    player->advance(5.0f * dt);
}
if (scene::Input::is_mouse_pressed(scene::MouseButton::Left)) {
    spawn_bullet();
}
scene::Input::end_frame();
```

---

### Color

`#include <scene/Color.hpp>`

32-bit ARGB color type (`0xAARRGGBB`).

```cpp
struct Color {
    uint32_t argb;
    // Convenience constructors and accessors
};
```

**Presets (11):** `White`, `Black`, `Red`, `Green`, `Blue`, `Yellow`,
`Cyan`, `Magenta`, `Orange`, `Purple`, `Gray`

**Key Methods:**

| Method | Description |
|--------|-------------|
| `from_rgb(r, g, b)` | Create from RGB (alpha = 255) |
| `from_rgba(r, g, b, a)` | Create from RGBA |
| `to_vec3()` / `to_vec4()` | Convert to normalized float |
| `to_hsv()` / `from_hsv(h, s, v)` | HSV conversion |
| `luminance()` | Perceived brightness `[0, 1]` |
| `lerp(a, b, t)` | Linear interpolation |
| `multiply(a, b)` | Component-wise multiply |

---

### Pixmap

`#include <scene/Pixmap.hpp>`

Software rasterizer for pixel-level image manipulation. Supports blend modes,
drawing primitives, and convolution filters.

```cpp
enum class BlendMode { None, Alpha, Additive, Multiply };
```

**Key Methods:**

| Method | Description |
|--------|-------------|
| `resize(int w, int h)` | Set dimensions |
| `set_pixel(int x, int y, Color)` | Write a pixel |
| `get_pixel(int x, int y)` | Read a pixel |
| `fill(Color)` | Fill entire pixmap |
| `draw_line(x0, y0, x1, y1, Color)` | Draw a line |
| `draw_rect(x, y, w, h, Color)` | Draw rectangle outline |
| `fill_rect(x, y, w, h, Color)` | Filled rectangle |
| `draw_circle(cx, cy, r, Color)` | Draw circle outline |
| `fill_circle(cx, cy, r, Color)` | Filled circle |
| `convolve(float kernel[9])` | Apply 3×3 convolution filter |
| `blur(int radius)` | Gaussian blur |
| `copy_to(Texture*)` | Upload to GPU texture |

---

### Collision

`#include <scene/Collision.hpp>`

Ellipsoid-based collision detection and response using the **Fauerby**
sliding algorithm. Works with triangle-level geometry accelerated by
`Tree.hpp` spatial structures.

```cpp
struct CollisionPacket {
    Vec3  eRadius;    // ellipsoid radii (X, Y, Z)
    Vec3  position;
    Vec3  velocity;
    // Internal recursion state
};

struct CollisionInfo {
    bool   collided;
    Vec3   hitPoint;
    Vec3   hitNormal;
    float  distance;
};

class CollisionSystem {
public:
    void add_mesh(MeshInstance* mesh);
    void update(float dt);
    bool check(CollisionPacket& packet, CollisionInfo& info);
};
```

The Fauerby algorithm:
1. Convert ellipsoid space to unit sphere.
2. Sweep the sphere against all nearby triangles.
3. If collision, slide along the collision plane.
4. Recurse with remaining velocity.

---

### Tree

`#include <scene/Tree.hpp>`

Spatial acceleration structures for broadphase collision detection.

**Quadtree:** Splits on the XZ plane (2D horizontal collision).

```cpp
class Quadtree {
    void insert(const BoundingBox& bounds, int triangleIndex);
    void query(const BoundingBox& area, std::vector<int>& results) const;
    void clear();
};
```

**Octree:** Full 3D spatial subdivision.

```cpp
class Octree {
    void insert(const BoundingBox& bounds, int triangleIndex);
    void query(const BoundingBox& area, std::vector<int>& results) const;
    void query(const Ray& ray, std::vector<int>& results) const;
    void clear();
};
```

Both structures operate at **triangle granularity** — each triangle in a mesh
is individually indexed for precise collision queries.

---

### IO

`#include <scene/IO.hpp>`

Platform abstraction for file I/O. Uses a **pluggable backend** pattern via
`FileInterface`.

```cpp
namespace io {
    class FileInterface {
    public:
        virtual bool exists(const char* path) = 0;
        virtual ByteArray readFile(const char* path) = 0;
        virtual bool writeFile(const char* path, const void* data, size_t size) = 0;
    };

    void registerFileInterface(const char* scheme, FileInterface* iface);
    FileInterface* getFileInterface(const char* scheme);
}
```

This allows the engine to read from any source — native filesystem, zip
archives, network, or custom virtual filesystems — by registering a backend
for each scheme.

---

### Filesystem

`#include <scene/Filesystem.hpp>`

Virtual filesystem with **search-path** resolution. Multiple folders and
archives can be mounted, and the system resolves paths in order.

```cpp
namespace fs {
    struct PathEntry {
        enum Type { FOLDER, ARCHIVE };
        Type        type;
        std::string path;
    };

    class Filesystem {
    public:
        static Filesystem& instance();
        void addFolder(const char* path);
        void addArchive(const char* path);
        std::string resolvePath(const char* relativePath);
        ByteArray readFile(const char* relativePath);
    };
}
```

**Limits:** `MAX_PATHS = 32`

```cpp
auto& fs = scene::fs::Filesystem::instance();
fs.addFolder("assets/");
fs.addFolder("mods/textures/");
fs.addArchive("data.pak");

// Resolves to first matching path in search order
ByteArray data = fs.readFile("textures/stone.png");
```

---

### ByteArray

`#include <scene/ByteArray.hpp>`

Move-only binary buffer with cursor-based typed read/write. Designed for
efficient mesh and asset loading.

```cpp
class ByteArray {
public:
    // Cursor-based reading
    int8_t   readI8();
    uint8_t  readU8();
    int16_t  readI16();
    uint16_t readU16();
    int32_t  readI32();
    uint32_t readU32();
    float    readF32();

    // Bulk reads (high-performance mesh loading)
    void readF32Array(float* out, size_t count);
    void readU32Array(uint32_t* out, size_t count);

    // Writing
    void writeF32(float v);
    void writeU32(uint32_t v);
    // ... etc.

    void setBigEndian(bool big);
    size_t size() const;
    size_t position() const;
    void seek(size_t pos);
};
```

The bulk read methods (`readF32Array`, `readU32Array`) are optimized for
loading large vertex and index buffers in a single call, avoiding per-element
overhead.

---

## Appendix: Node Type Reference

| NodeType | Header | Base Class |
|----------|--------|------------|
| `NT_NODE` | `Node.hpp` | — |
| `NT_NODE3D` | `Node3D.hpp` | `Node` |
| `NT_MESHINSTANCE` | `MeshInstance.hpp` | `Node3D` |
| `NT_LIGHT` | `LightNode.hpp` | `Node3D` |
| `NT_CAMERA` | `Camera3D.hpp` | `Node3D` |
| `NT_SKINNEDMESH` | `SkinnedMeshInstance.hpp` | `Node3D` |
| `NT_BONEATTACHMENT` | `BoneAttachment.hpp` | `Node3D` |
| `NT_TAGATTACHMENT` | `TagAttachment.hpp` | `Node3D` |
| `NT_PARTICLESYSTEM` | `ParticleSystemNode.hpp` | `Node3D` |
| `NT_GRASSSYSTEM` | `GrassSystemNode.hpp` | `Node3D` |
| `NT_DECALSYSTEM` | `DecalSystemNode.hpp` | `Node3D` |
| `NT_RIBBONTRAIL` | `RibbonTrailNode.hpp` | `Node3D` |
| `NT_LENSFLARE` | `LensFlareNode.hpp` | `Node3D` |
| `NT_WATER` | `WaterNode.hpp` | `Node3D` |
| `NT_OCEAN` | `OceanNode.hpp` | `WaterNode` |
| `NT_MIRROR` | `MirrorNode.hpp` | `Node3D` |
| `NT_TERRAIN` | `TerrainNode.hpp` | `MeshInstance` |
| `NT_INFINITETERRAIN` | `InfiniteTerrainNode.hpp` | `Node3D` |
| `NT_TERRAINLOD` | `TerrainLodNode.hpp` | `Node3D` |
| `NT_TERRAINPAGING` | `TerrainPagingNode.hpp` | `Node3D` |
| `NT_TILEDTERRAIN` | `TiledTerrainNode.hpp` | `Node3D` |
| `NT_VOLUME` | `Volume.hpp` | `Node3D` |
| `NT_BSPINSTANCE` | `BspInstance.hpp` | `Node3D` |
| `NT_BEHAVIOR` | `Behavior.hpp` | `Node` |
