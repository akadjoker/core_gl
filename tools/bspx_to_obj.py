#!/usr/bin/env python3

# Exports a .bsx (see bspx_to_h3d.py) to plain Wavefront .obj + .mtl — so it
# can be opened straight in Blender, textured, for a human to look at/fix
# directly (e.g. the shadow peter-panning chase turned out to be a rabbit
# hole; raising the floor surface a hair by hand in Blender closes the same
# gap without fighting shadow-bias math at all). Reuses bspx_to_h3d's own
# parsing/triangulation/texture-resolution (parse_and_triangulate,
# resolve_textures) so both tools see identical geometry and textures —
# this is a pure alternate writer, not a separate importer.
#
# Round-trip back into the engine: export the edited mesh from Blender as
# .obj again, then run obj_to_h3d.py (companion script) to rebuild the
# .h3d the engine actually loads.

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bspx_to_h3d import parse_and_triangulate, resolve_textures, Vec3


def write_mtl(mtl_path, textures, used_tex_ids, resolved_tex):
    with open(mtl_path, "w") as f:
        for tid in used_tex_ids:
            mat_name = os.path.basename(textures[tid]) if tid < len(textures) else f"tex{tid}"
            f.write(f"newmtl {mat_name}\n")
            f.write("Kd 1.0 1.0 1.0\n")
            f.write("Ks 0.0 0.0 0.0\n")
            tex = resolved_tex.get(tid)
            if tex:
                f.write(f"map_Kd {tex}\n")
            f.write("\n")
    print(f"wrote {mtl_path}")


def write_obj(out_path, mtl_name, tris, textures, used_tex_ids, scale=1.0):
    # position (v) is WELDED across the whole file — two triangles that
    # share a corner in world space share the same v index, so Blender
    # sees a connected mesh (select-linked grabs the whole floor, moving a
    # shared vertex drags every triangle touching it) instead of 237
    # disconnected single-triangle islands. UV (vt) and the flat normal
    # (vn) stay per-corner/per-triangle — welding those too would force a
    # single UV and a smooth normal across a seam or a hard edge, which
    # isn't what this geometry is.
    #
    # scale: the .bsx's own raw units (thousands) aren't the engine's world
    # units — MeshInstance::set_scale(kModelScale) applies that at runtime.
    # Editing at raw scale in Blender means every move/snap is meaningless
    # relative to the actual game (a player is ~1 world unit tall). Passing
    # the same kModelScale here exports at real world scale instead, so it
    # sits at the same scale as other hand-authored assets (e.g.
    # assets/models/house.obj).
    vert_index = {}  # rounded (x,y,z) -> 1-based v index
    vert_lines = []

    def v_index(p):
        sx, sy, sz = p.x * scale, p.y * scale, p.z * scale
        key = (round(sx, 4), round(sy, 4), round(sz, 4))
        idx = vert_index.get(key)
        if idx is None:
            idx = len(vert_index) + 1
            vert_index[key] = idx
            vert_lines.append(f"v {sx:.6f} {sy:.6f} {sz:.6f}\n")
        return idx

    by_material = {}
    for tri in tris:
        by_material.setdefault(tri[6], []).append(tri)

    vt_lines = []
    vn_lines = []
    face_lines = []  # (group_header, [f line])
    vt_count = 0

    for tid in used_tex_ids:
        tris_here = by_material.get(tid, [])
        if not tris_here:
            continue
        mat_name = os.path.basename(textures[tid]) if tid < len(textures) else f"tex{tid}"
        group_faces = []
        for (p0, p1, p2, uv0, uv1, uv2, _tid) in tris_here:
            vidx = [v_index(p) for p in (p0, p1, p2)]
            for uv in (uv0, uv1, uv2):
                vt_lines.append(f"vt {uv[0]:.6f} {1.0 - uv[1]:.6f}\n")
            vtidx = [vt_count + 1, vt_count + 2, vt_count + 3]
            vt_count += 3

            n = (p1 - p0).cross(p2 - p0)
            nlen = n.length()
            if nlen > 1e-8:
                n = Vec3(n.x / nlen, n.y / nlen, n.z / nlen)
            vn_lines.append(f"vn {n.x:.6f} {n.y:.6f} {n.z:.6f}\n")
            vnidx = len(vn_lines)

            group_faces.append(
                f"f {vidx[0]}/{vtidx[0]}/{vnidx} {vidx[1]}/{vtidx[1]}/{vnidx} "
                f"{vidx[2]}/{vtidx[2]}/{vnidx}\n")
        face_lines.append((mat_name, group_faces))

    with open(out_path, "w") as f:
        f.write("# exported by bspx_to_obj.py\n")
        f.write(f"mtllib {mtl_name}\n")
        f.writelines(vert_lines)
        f.writelines(vt_lines)
        f.writelines(vn_lines)
        for mat_name, faces in face_lines:
            f.write(f"g mat_{mat_name}\n")
            f.write(f"usemtl {mat_name}\n")
            f.writelines(faces)

    tri_count = sum(len(faces) for _, faces in face_lines)
    print(f"wrote {out_path} ({len(vert_lines)} verts, {tri_count} triangles)")


def convert_to_obj(bsx_path, textures_root, out_path, scale=1.0, patch_subdiv=6):
    tris, textures = parse_and_triangulate(bsx_path, patch_subdiv)
    used_tex_ids, resolved_tex = resolve_textures(tris, textures, textures_root, out_path)

    mtl_path = os.path.splitext(out_path)[0] + ".mtl"
    write_mtl(mtl_path, textures, used_tex_ids, resolved_tex)
    write_obj(out_path, os.path.basename(mtl_path), tris, textures, used_tex_ids, scale)


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        print(f"usage: {sys.argv[0]} <map.bsx> <textures_dir> <out.obj> [scale]")
        sys.exit(1)
    scale = float(sys.argv[4]) if len(sys.argv) == 5 else 1.0
    convert_to_obj(sys.argv[1], sys.argv[2], sys.argv[3], scale)
