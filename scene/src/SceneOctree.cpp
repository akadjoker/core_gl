#include "scene/SceneOctree.hpp"
#include "scene/MeshInstance.hpp"
#include "scene/Mesh.hpp"
#include "scene/Node.hpp"

namespace
{
// Loose octree: each node's effective (query) bounds are 2x its own tight
// split region, centered the same, overlapping its siblings by half a cell —
// same trick Ogre's OctreeSceneManager uses. This is what makes real-world
// geometry (a power plant's long pipes/beams, not neatly-sized props) actually
// subdivide: an entry only needs to be <= a child's TIGHT size to be routed
// into it, regardless of where within the parent its center falls, because
// the child's LOOSE region always has enough slack to fully contain it.
BoundingBox looseBounds(const SceneOctreeNode* node)
{
    Vec3 c = node->bounds.center();
    Vec3 size = node->bounds.max - node->bounds.min; // tight full size
    return BoundingBox(c - size, c + size);
}

// which child octant an entry's center falls into — always exactly one,
// no ambiguity, matches the bit layout SceneOctreeNode::split() builds
// (bit0=x>=center, bit1=y>=center, bit2=z>=center)
int childIndexFor(const Vec3& point, const Vec3& center)
{
    int idx = 0;
    if (point.x >= center.x) idx |= 1;
    if (point.y >= center.y) idx |= 2;
    if (point.z >= center.z) idx |= 4;
    return idx;
}

void gather_entries(Node* node, std::vector<SceneOctreeEntry>& out)
{
    MeshInstance* mi = node->as<MeshInstance>();
    if (mi && mi->visible && mi->get_mesh() && mi->get_mesh()->is_uploaded())
    {
        Mesh* mesh = mi->get_mesh();
        const Mat4& world = mi->get_world_matrix();
        const std::vector<Surface>& surfaces = mesh->surfaces();
        for (u32 s = 0; s < (u32)surfaces.size(); ++s)
        {
            BoundingBox bb = BoundingBox::TransformBoundingBox(surfaces[s].bounds, world);
            out.push_back(SceneOctreeEntry(mi, s, bb));
        }
    }

    for (Node* child : node->get_children())
        gather_entries(child, out);
}
} // namespace

SceneOctreeNode::~SceneOctreeNode()
{
    for (SceneOctreeNode* c : children)
        delete c;
}

void SceneOctreeNode::split()
{
    if (!isLeaf) return;
    isLeaf = false;
    Vec3 mn = bounds.min, mx = bounds.max, c = bounds.center();

    children[0] = new SceneOctreeNode(BoundingBox(Vec3(mn.x, mn.y, mn.z), Vec3(c.x, c.y, c.z)), depth + 1);
    children[1] = new SceneOctreeNode(BoundingBox(Vec3(c.x, mn.y, mn.z), Vec3(mx.x, c.y, c.z)), depth + 1);
    children[2] = new SceneOctreeNode(BoundingBox(Vec3(mn.x, c.y, mn.z), Vec3(c.x, mx.y, c.z)), depth + 1);
    children[3] = new SceneOctreeNode(BoundingBox(Vec3(c.x, c.y, mn.z), Vec3(mx.x, mx.y, c.z)), depth + 1);
    children[4] = new SceneOctreeNode(BoundingBox(Vec3(mn.x, mn.y, c.z), Vec3(c.x, c.y, mx.z)), depth + 1);
    children[5] = new SceneOctreeNode(BoundingBox(Vec3(c.x, mn.y, c.z), Vec3(mx.x, c.y, mx.z)), depth + 1);
    children[6] = new SceneOctreeNode(BoundingBox(Vec3(mn.x, c.y, c.z), Vec3(c.x, mx.y, mx.z)), depth + 1);
    children[7] = new SceneOctreeNode(BoundingBox(Vec3(c.x, c.y, c.z), Vec3(mx.x, mx.y, mx.z)), depth + 1);
}

SceneOctree::SceneOctree(int maxDepth, int maxEntriesPerNode)
    : m_maxDepth(maxDepth), m_maxEntriesPerNode(maxEntriesPerNode)
{
}

SceneOctree::~SceneOctree() { clear(); }

void SceneOctree::clear()
{
    delete m_root;
    m_root = nullptr;
}

void SceneOctree::build(Node* root)
{
    clear();
    if (!root) return;

    std::vector<SceneOctreeEntry> entries;
    gather_entries(root, entries);
    if (entries.empty()) return;

    BoundingBox worldBounds = entries[0].worldBounds;
    for (size_t i = 1; i < entries.size(); ++i)
        worldBounds.Merge(entries[i].worldBounds);

    // Pad to a cube (same extent on all 3 axes, same center). A flat/wide
    // scene (a field of props, all near the same Y) has a root box far
    // wider in X/Z than in Y; split() halves every axis at once, so Y
    // shrinks below a single entry's own height within 1-2 levels and the
    // fit test (entrySize <= childSize on ALL axes) then blocks EVERY
    // entry from ever descending past the root on every axis, not just Y —
    // that's what "1 occupied node, depth 0" for 19600 scattered props was.
    // Padding keeps every axis shrinking at the same rate, so X/Z splitting
    // isn't held hostage by a thin Y.
    {
        Vec3 size = worldBounds.max - worldBounds.min;
        float maxExtent = size.x;
        if (size.y > maxExtent) maxExtent = size.y;
        if (size.z > maxExtent) maxExtent = size.z;
        Vec3 c = worldBounds.center();
        Vec3 half(maxExtent * 0.5f, maxExtent * 0.5f, maxExtent * 0.5f);
        worldBounds = BoundingBox(c - half, c + half);
    }

    m_root = new SceneOctreeNode(worldBounds, 0);
    for (const SceneOctreeEntry& e : entries)
        insertRecursive(m_root, e);
}

void SceneOctree::insert(MeshInstance* instance, unsigned int surfaceIndex,
                         const BoundingBox& worldBounds)
{
    if (!m_root) m_root = new SceneOctreeNode(worldBounds, 0);
    insertRecursive(m_root, SceneOctreeEntry(instance, surfaceIndex, worldBounds));
}

void SceneOctree::insertRecursive(SceneOctreeNode* node, const SceneOctreeEntry& e)
{
    if (node->isLeaf)
    {
        node->entries.push_back(e);
        if ((int)node->entries.size() > m_maxEntriesPerNode && node->depth < m_maxDepth)
        {
            node->split();
            std::vector<SceneOctreeEntry> ex = std::move(node->entries);
            node->entries.clear();
            for (const SceneOctreeEntry& ent : ex)
                insertRecursive(node, ent); // re-route now that node has children
        }
        return;
    }

    Vec3 c = node->bounds.center();
    int idx = childIndexFor(e.worldBounds.center(), c);
    SceneOctreeNode* child = node->children[idx];
    Vec3 entrySize = e.worldBounds.max - e.worldBounds.min;
    Vec3 childSize = child->bounds.max - child->bounds.min;

    // fits (loosely) into that one child regardless of where its center
    // lands, as long as it isn't bigger than the child's own tight region
    if (entrySize.x <= childSize.x && entrySize.y <= childSize.y && entrySize.z <= childSize.z)
    {
        insertRecursive(child, e);
        return;
    }
    // too big for any single child at this depth — stays at this node
    node->entries.push_back(e);
}

void SceneOctree::queryFrustum(const Frustum& frustum, std::vector<SceneOctreeEntry>& out) const
{
    queryFrustumRec(m_root, frustum, out);
}

void SceneOctree::queryFrustumRec(const SceneOctreeNode* node, const Frustum& frustum,
                                  std::vector<SceneOctreeEntry>& out) const
{
    if (!node || !frustum.ContainsBox(looseBounds(node))) return;
    for (const SceneOctreeEntry& e : node->entries)
        out.push_back(e);
    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i)
            queryFrustumRec(node->children[i], frustum, out);
}
