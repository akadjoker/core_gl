#pragma once

#include "scene/Math.hpp" // BoundingBox, Frustum
#include <vector>

class Node;
class MeshInstance;

// Surface-level spatial index for frustum culling, sibling to the
// triangle-granularity Octree/Quadtree in Tree.hpp (that one is for collision
// broadphase, not render culling). Indexes individual Surfaces (not whole
// MeshInstances) by world AABB, so Scene::collect() can reject whole subtrees
// instead of walking every Node and testing every Surface's bounds each call.
//
// Surface granularity, not instance granularity, matters: a scene can be one
// MeshInstance holding hundreds of surfaces (Sponza, powerplant — the whole
// level loaded as a single mesh with many material chunks). Indexing by
// MeshInstance alone would give the octree a single entry for the entire
// level and never split. Indexing by surface means each architectural piece
// gets its own leaf, same as scenes built from many small MeshInstances.
//
// Static geometry only: built once (see build()) after the scene is populated.
// Moving/adding/removing MeshInstances after build() requires calling build()
// again — there is no live add/remove tracking in this version.

struct SceneOctreeEntry
{
    MeshInstance* instance;
    unsigned int surfaceIndex;
    BoundingBox worldBounds; // one surface's bounds, transformed by get_world_matrix()

    SceneOctreeEntry(MeshInstance* inst, unsigned int surf, const BoundingBox& b)
        : instance(inst), surfaceIndex(surf), worldBounds(b)
    {
    }
};

struct SceneOctreeNode
{
    BoundingBox bounds;
    SceneOctreeNode* children[8] = {};
    std::vector<SceneOctreeEntry> entries;
    int depth = 0;
    bool isLeaf = true;

    explicit SceneOctreeNode(const BoundingBox& b, int d) : bounds(b), depth(d) {}
    ~SceneOctreeNode();
    void split();
};

class SceneOctree
{
public:
    explicit SceneOctree(int maxDepth = 8, int maxEntriesPerNode = 8);
    ~SceneOctree();

    void clear();

    // Walks the Node tree once, computes each visible MeshInstance's surfaces'
    // world AABBs and inserts one entry per surface. Two passes: first merges
    // every surface AABB to size the root bounds, second inserts each one.
    void build(Node* root);

    void insert(MeshInstance* instance, unsigned int surfaceIndex, const BoundingBox& worldBounds);

    // Appends every surface entry whose containing octree node's bounds pass
    // the frustum test (coarse at the node level — callers still do their own
    // precise per-surface AABB test using entry.worldBounds, same as the
    // non-indexed path).
    void queryFrustum(const Frustum& frustum, std::vector<SceneOctreeEntry>& out) const;

    bool empty() const { return m_root == nullptr; }

    // read-only access to the tree for debug visualization (draw node bounds)
    const SceneOctreeNode* root() const { return m_root; }

private:
    SceneOctreeNode* m_root = nullptr;
    int m_maxDepth;
    int m_maxEntriesPerNode;

    void insertRecursive(SceneOctreeNode* node, const SceneOctreeEntry& e);
    void queryFrustumRec(const SceneOctreeNode* node, const Frustum& frustum,
                          std::vector<SceneOctreeEntry>& out) const;
};
