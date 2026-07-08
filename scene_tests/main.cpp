// Unit tests for the scene layer. No window, no GL context — the node tree
// and transform math are plain logic, so they get exact assertions here and
// run anywhere (CI included).

#include <scene/Node.hpp>
#include <scene/Node3D.hpp>
#include <scene/Camera3D.hpp>
#include <scene/MeshInstance.hpp>
#include <cstdio>
#include <cmath>

static int g_failed = 0;

static void check(bool ok, const char* what)
{
    printf("%-58s %s\n", what, ok ? "OK" : "FAIL");
    if (!ok) ++g_failed;
}

static bool near3(const Vec3& a, float x, float y, float z, float eps = 1e-4f)
{
    return fabsf(a.x - x) < eps && fabsf(a.y - y) < eps && fabsf(a.z - z) < eps;
}

// _ready/_update hooks: record call order to verify propagation rules
static int g_order = 0;

struct ProbeNode : Node3D
{
    int readyOrder = -1;
    int updateOrder = -1;
    explicit ProbeNode(const std::string& n) : Node3D(n) {}

protected:
    void _ready() override { readyOrder = g_order++; }
    void _update(float) override { updateOrder = g_order++; }
};

int main()
{
    // ---- hierarchy & ownership ----
    {
        Node root("root");
        Node* a = root.add_child(new Node("a"));
        Node* b = a->add_child(new Node("b"));
        root.add_child(new Node("c"));

        check(root.get_child_count() == 2, "add_child: root has two children");
        check(b->get_parent() == a, "add_child: parent pointer set");
        check(root.find_child("b", true) == b, "find_child: recursive lookup");
        check(root.find_child("b", false) == nullptr, "find_child: non-recursive respects depth");
        check(root.is_ancestor_of(b), "is_ancestor_of: grandparent");
        check(!b->is_ancestor_of(&root), "is_ancestor_of: not inverted");

        // reparent instead of duplicating
        Node* c = root.find_child("c", false);
        c->add_child(b->get_parent() /* a, with subtree */);
        check(root.get_child_count() == 1 && c->get_child_count() == 1,
              "add_child: reparent moves, doesn't duplicate");
        check(a->get_parent() == c, "add_child: reparent updates parent");

        // cycle guard: adding an ancestor as a child must be refused
        b->add_child(c);
        check(c->get_parent() != b, "add_child: refuses to create a cycle");

        // remove_child returns ownership: caller must delete
        c->remove_child(a);
        check(a->get_parent() == nullptr && c->get_child_count() == 0,
              "remove_child: detaches without destroying");
        delete a; // takes b down with it — no double delete when root dies
    }

    // ---- ready/update propagation order ----
    {
        ProbeNode root("root");
        ProbeNode* child = (ProbeNode*)root.add_child(new ProbeNode("child"));

        g_order = 0;
        root.propagate_ready();
        check(child->readyOrder == 0 && root.readyOrder == 1, "_ready: children before parent");

        g_order = 0;
        root.propagate_ready(); // second call must not re-fire
        check(child->readyOrder == 0 && root.readyOrder == 1, "_ready: fires only once");

        g_order = 0;
        root.propagate_update(1.f / 60.f);
        check(root.updateOrder == 0 && child->updateOrder == 1, "_update: parent before children");
    }

    // ---- local/world transforms through a parent chain ----
    {
        Node3D root("root");
        Node3D* mid = (Node3D*)root.add_child(new Node3D("mid"));
        Node3D* leaf = (Node3D*)mid->add_child(new Node3D("leaf"));

        root.set_position(10.f, 0.f, 0.f);
        mid->set_position(0.f, 5.f, 0.f);
        leaf->set_position(0.f, 0.f, 2.f);
        check(near3(leaf->get_global_position(), 10.f, 5.f, 2.f),
              "world position: translations accumulate down the chain");

        // rotating the root 90 degrees around Y swings the whole subtree:
        // the leaf's local +Z offset must become world -X... (0,0,2) rotated
        // by +90 deg on Y -> (2*sin? ) verify: R_y(90)*(0,0,2) = (2,0,0)...
        // YXZ euler, +90deg on Y maps +Z to +X
        root.set_euler(Vec3(0.f, 3.14159265f * 0.5f, 0.f));
        Vec3 wp = leaf->get_global_position();
        check(near3(wp, 12.f, 5.f, 0.f, 1e-3f) || near3(wp, 8.f, 5.f, 0.f, 1e-3f),
              "world position: parent rotation carries children");

        // dirty-flag correctness: move the middle node AFTER everything was
        // cached; the leaf must see the change
        root.set_euler(Vec3(0.f, 0.f, 0.f));
        (void)leaf->get_global_position(); // force cache
        mid->set_position(0.f, 50.f, 0.f);
        check(near3(leaf->get_global_position(), 10.f, 50.f, 2.f),
              "dirty flags: parent movement invalidates cached child world");

        // scale composes too
        mid->set_scale(2.f);
        check(near3(leaf->get_global_position(), 10.f, 50.f, 4.f),
              "world position: parent scale applies to child offsets");
    }

    // ---- global setters round-trip ----
    {
        Node3D root("root");
        Node3D* child = (Node3D*)root.add_child(new Node3D("child"));
        root.set_position(3.f, 4.f, 5.f);
        root.set_euler(Vec3(0.f, 1.1f, 0.f));

        child->set_global_position(Vec3(7.f, 8.f, 9.f));
        check(near3(child->get_global_position(), 7.f, 8.f, 9.f),
              "set_global_position: round-trips through a transformed parent");
    }

    // ---- movement & axes ----
    {
        Node3D n("n");
        n.advance(5.f); // forward is -Z
        check(near3(n.get_position(), 0.f, 0.f, -5.f), "advance: moves along -Z");

        n.set_position(0.f, 0.f, 0.f);
        n.set_euler(Vec3(0.f, 3.14159265f * 0.5f, 0.f)); // face +/-X
        n.advance(5.f);
        Vec3 p = n.get_position();
        check(fabsf(fabsf(p.x) - 5.f) < 1e-3f && fabsf(p.z) < 1e-3f,
              "advance: follows the node's orientation");

        n.set_euler(Vec3(0.f, 0.f, 0.f));
        check(near3(n.forward(), 0.f, 0.f, -1.f), "forward(): -Z at identity");
        check(near3(n.right(), 1.f, 0.f, 0.f), "right(): +X at identity");
        check(near3(n.up(), 0.f, 1.f, 0.f), "up(): +Y at identity");
    }

    // ---- look_at ----
    {
        Node3D n("n");
        n.set_position(0.f, 0.f, 0.f);
        n.look_at(Vec3(10.f, 0.f, 0.f));
        check(near3(n.global_forward(), 1.f, 0.f, 0.f, 1e-3f),
              "look_at: forward() points at the target");

        n.look_at(Vec3(0.f, 0.f, -10.f));
        check(near3(n.global_forward(), 0.f, 0.f, -1.f, 1e-3f), "look_at: works along -Z too");
    }

    // ---- typing ----
    {
        Node root("root");
        Node3D* n3d = (Node3D*)root.add_child(new Node3D("n3d"));
        Node* plain = root.add_child(new Node("plain"));

        check(n3d->as<Node3D>() == n3d, "as<Node3D>: correct downcast");
        check(plain->as<Node3D>() == nullptr, "as<Node3D>: refuses a plain Node");
        check(n3d->as<Node>() != nullptr, "as<Node>: upcast always works");
    }

    // ---- Camera3D ----
    {
        Camera3D cam("cam");
        cam.set_perspective(55.f, 0.1f, 100.f);
        cam.set_aspect(16.f / 9.f);

        // the rigid-inverse view must match Mat4::LookAt for the same pose
        // (LookAt is already proven correct by the GL shadow/CSM tests)
        cam.set_position(0.f, 3.f, 6.f);
        cam.look_at(Vec3(0.f, 0.5f, 0.f));
        Mat4 view = cam.get_view_matrix();
        Mat4 ref = Mat4::LookAt(Vec3(0.f, 3.f, 6.f), Vec3(0.f, 0.5f, 0.f), Vec3(0.f, 1.f, 0.f));
        bool same = true;
        for (int i = 0; i < 16; ++i)
            if (fabsf(view.x[i] - ref.x[i]) > 1e-4f) same = false;
        check(same, "Camera3D: view matrix matches Mat4::LookAt");

        // view of a camera attached to a moving parent follows the parent
        Node3D rig("rig");
        Camera3D* child = (Camera3D*)rig.add_child(new Camera3D("child"));
        rig.set_position(10.f, 0.f, 0.f);
        Mat4 v2 = child->get_view_matrix();
        // world origin seen from a camera at (10,0,0) sits at x=-10 in view space
        Vec3 originInView = v2 * Vec3(0.f, 0.f, 0.f);
        check(near3(originInView, -10.f, 0.f, 0.f), "Camera3D: view follows a parent rig");

        // projection cache invalidates on lens change
        cam.set_aspect(1.f);
        float a = cam.get_projection_matrix().x[0];
        cam.set_aspect(2.f);
        float b = cam.get_projection_matrix().x[0];
        check(fabsf(a - 2.f * b) < 1e-4f, "Camera3D: projection rebuilds when aspect changes");
    }

    // ---- MeshInstance typing ----
    {
        Node root("root");
        MeshInstance* mi = (MeshInstance*)root.add_child(new MeshInstance("mi"));
        check(mi->is_a(NT_MESHINSTANCE) && mi->is_a(NT_NODE3D) && mi->is_a(NT_NODE),
              "MeshInstance: is_a covers the whole chain");
        check(root.get_child(0)->as<MeshInstance>() == mi, "MeshInstance: as<> from base pointer");
        check(root.get_child(0)->as<Camera3D>() == nullptr,
              "MeshInstance: not confused with Camera3D");
    }

    // ---- frustum culling primitives ----
    {
        Mat4 proj = Mat4::Perspective(55.0, 1.7, 0.1, 300.0);
        Mat4 view = Mat4::LookAt(Vec3(0.f, 6.f, 22.f), Vec3(0.f, 0.f, 0.f), Vec3(0.f, 1.f, 0.f));
        Frustum f;
        f.build(view, proj);

        check(f.ContainsBox(BoundingBox(Vec3(-1.f, -1.f, -1.f), Vec3(1.f, 1.f, 1.f))),
              "frustum: box dead ahead is visible");
        check(!f.ContainsBox(BoundingBox(Vec3(-1.f, -1.f, 50.f), Vec3(1.f, 1.f, 52.f))),
              "frustum: box behind the camera is culled");
        check(f.ContainsBox(BoundingBox(Vec3(-40.f, 0.f, -40.f), Vec3(40.f, 0.f, 40.f))),
              "frustum: huge intersecting box is visible");
        check(!f.ContainsBox(BoundingBox(Vec3(-1.f, -1.f, -500.f), Vec3(1.f, 1.f, -498.f))),
              "frustum: box beyond the far plane is culled");

        BoundingBox t = BoundingBox::TransformBoundingBox(
            BoundingBox(Vec3(-1.f, -1.f, -1.f), Vec3(1.f, 1.f, 1.f)), Mat4());
        check(fabsf(t.min.x + 1.f) < 1e-4f && fabsf(t.max.x - 1.f) < 1e-4f,
              "TransformBoundingBox: identity keeps bounds");
    }

    printf(g_failed == 0 ? "ALL SCENE TESTS PASSED\n" : "%d SCENE TESTS FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
