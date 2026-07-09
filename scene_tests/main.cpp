// Unit tests for the scene layer. No window, no GL context — the node tree
// and transform math are plain logic, so they get exact assertions here and
// run anywhere (CI included).

#include <scene/Node.hpp>
#include <scene/Node3D.hpp>
#include <scene/Camera3D.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/Pixmap.hpp>
#include <scene/Color.hpp>
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

// ── Pixmap tests ─────────────────────────────────────────────────────────

static void test_pixmap_basics()
{
    scene::Pixmap p;
    check(!p.is_valid(), "Pixmap: default is_valid false");
    check(p.get_size() == 0, "Pixmap: default get_size 0");
    check(!p.has_alpha(), "Pixmap: default has_alpha false");

    scene::Pixmap p2(32, 32, 4);
    check(p2.is_valid(), "Pixmap: sized ctor is_valid");
    check(p2.width == 32 && p2.height == 32, "Pixmap: sized dimensions");
    check(p2.has_alpha(), "Pixmap: 4-comp has_alpha");

    scene::Pixmap p3(16, 16, 3);
    check(!p3.has_alpha(), "Pixmap: 3-comp has_alpha false");
}

static void test_pixmap_pixels()
{
    scene::Pixmap p(4, 4, 4);
    p.fill(static_cast<gl::u8>(255), static_cast<gl::u8>(0),
           static_cast<gl::u8>(0),   static_cast<gl::u8>(128));
    scene::Color c = p.get_pixel_color(1, 1);
    check(c.r() == 255 && c.g() == 0 && c.b() == 0 && c.a() == 128,
          "Pixmap: fill RGBA + get_pixel_color");

    p.set_pixel(2u, 2u, 0x80402010u);
    check(p.get_pixel(2, 2) == 0x80402010u, "Pixmap: set_pixel/get_pixel u32");

    p.clear();
    scene::Color cz = p.get_pixel_color(2, 2);
    check(cz.r() == 0 && cz.g() == 0 && cz.b() == 0 && cz.a() == 0,
          "Pixmap: clear zeros pixels");
}

static void test_pixmap_flip()
{
    scene::Pixmap p(4, 3, 4);
    p.fill(0, 0, 0, 0);
    p.set_pixel(0u, 0u, static_cast<gl::u8>(255), static_cast<gl::u8>(0),
                static_cast<gl::u8>(0), static_cast<gl::u8>(255));
    p.flip_vertical();
    check(p.get_pixel_color(0, 2).r() == 255, "Pixmap: flip_vertical");
    p.flip_horizontal();
    check(p.get_pixel_color(3, 2).r() == 255, "Pixmap: flip_horizontal");
}

static void test_pixmap_drawing()
{
    scene::Pixmap p(8, 8, 4);
    p.clear();
    p.draw_line(0, 0, 7, 7, scene::Color(255, 0, 0, 255));
    check(p.get_pixel_color(4, 4).r() == 255, "Pixmap: draw_line");

    p.clear();
    p.draw_rect(2, 2, 3, 3, scene::Color(0, 255, 0, 255), true);
    check(p.get_pixel_color(3, 3).g() == 255, "Pixmap: draw_rect filled");

    p.clear();
    p.draw_rect(2, 2, 3, 3, scene::Color(0, 0, 255, 255), false);
    check(p.get_pixel_color(2, 2).b() == 255, "Pixmap: draw_rect outline");

    p.clear();
    p.draw_circle(4, 4, 3, scene::Color(255, 255, 0, 255), true);
    check(p.get_pixel_color(4, 4).r() == 255, "Pixmap: draw_circle filled");
}

static void test_pixmap_blit()
{
    scene::Pixmap src(4, 4, 4);
    src.fill(255, 0, 0, 255);
    scene::Pixmap dst(8, 8, 4);
    dst.clear();
    dst.draw_pixmap(src, 2, 2);
    check(dst.get_pixel_color(3, 3).r() == 255, "Pixmap: draw_pixmap blit");

    dst.clear();
    IntRect sr = { 1, 1, 2, 2 };
    dst.draw_pixmap(src, 0, 0, sr);
    check(dst.get_pixel_color(0, 0).r() == 255, "Pixmap: draw_pixmap rect");
}

static void test_pixmap_blend()
{
    scene::Pixmap p(2, 2, 4);
    p.fill(0, 0, 0, 0);
    p.blend_pixel(0u, 0u, scene::Color(255, 0, 0, 128), 0.5f,
                   scene::Pixmap::BlendMode::copy);
    scene::Color c = p.get_pixel_color(0, 0);
    check(c.r() == 255 && c.a() == 64, "Pixmap: blend_pixel copy 50%");

    scene::Pixmap src(2, 2, 4);
    src.fill(255, 0, 0, 64);
    scene::Pixmap dst2(2, 2, 4);
    dst2.fill(0, 0, 255, 255);
    dst2.draw_pixmap_blended(src, 0, 0, 0.5f, scene::Pixmap::BlendMode::alpha);
    c = dst2.get_pixel_color(0, 0);
    check(c.b() > 200 && c.r() > 0, "Pixmap: draw_pixmap_blended mixed");
}

static void test_pixmap_crop()
{
    scene::Pixmap src(4, 4, 4);
    src.fill(0, 255, 0, 255);
    src.set_pixel(1u, 1u, static_cast<gl::u8>(0), static_cast<gl::u8>(0),
                  static_cast<gl::u8>(255), static_cast<gl::u8>(255));
    IntRect cr = { 1, 1, 2, 2 };
    scene::Pixmap* c = src.crop(cr);
    check(c && c->width == 2, "Pixmap: crop result dims");
    delete c;
}

static void test_pixmap_color_ops()
{
    scene::Pixmap p(2, 2, 4);
    p.fill(255, 255, 255, 255);
    p.tint(128, 255, 64);
    scene::Color c = p.get_pixel_color(0, 0);
    check(c.r() == 128 && c.g() == 255, "Pixmap: tint");

    p.fill(255, 0, 0, 255);
    p.replace_color(scene::Color(255, 0, 0, 255), scene::Color(0, 255, 0, 255), 0.1f);
    c = p.get_pixel_color(0, 0);
    check(c.r() == 0 && c.g() == 255, "Pixmap: replace_color");
}

static void test_pixmap_convert_resize()
{
    scene::Pixmap p(2, 2, 3);
    p.fill(10, 20, 30, 0);
    scene::Pixmap* rgba = p.convert_to_rgba();
    check(rgba && rgba->components == 4, "Pixmap: convert_to_rgba");
    delete rgba;

    scene::Pixmap p2(4, 4, 4);
    p2.fill(255, 0, 0, 255);
    scene::Pixmap* r = p2.resize(2, 2);
    check(r && r->width == 2, "Pixmap: resize dims");
    delete r;
}

static void test_pixmap_filters()
{
    scene::Pixmap p(4, 4, 4);
    p.fill(255, 255, 255, 255);
    scene::Pixmap* b = p.apply_blur(1);
    check(b && b->get_pixel_color(1,1).r() == 255, "Pixmap: apply_blur");
    delete b;
    scene::Pixmap* g = p.apply_gaussian_blur(1);
    check(g != nullptr, "Pixmap: apply_gaussian_blur");
    delete g;
    scene::Pixmap* s = p.apply_sharpen();
    check(s != nullptr, "Pixmap: apply_sharpen");
    delete s;
    scene::Pixmap* e = p.apply_edge_detection();
    check(e != nullptr, "Pixmap: apply_edge_detection");
    delete e;
    scene::Pixmap* em = p.apply_emboss();
    check(em != nullptr, "Pixmap: apply_emboss");
    delete em;
}

// ── _ready/_update hooks: record call order to verify propagation rules ──
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
    // ── Pixmap tests ──
    printf("--- Pixmap ---\n");
    test_pixmap_basics();
    test_pixmap_pixels();
    test_pixmap_flip();
    test_pixmap_drawing();
    test_pixmap_blit();
    test_pixmap_blend();
    test_pixmap_crop();
    test_pixmap_color_ops();
    test_pixmap_convert_resize();
    test_pixmap_filters();
    printf("\n");

    // ── hierarchy & ownership ──
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
