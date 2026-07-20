#!/usr/bin/env python3
 
import struct
import sys
import os

def read_struct(f, fmt):
    size = struct.calcsize(fmt)
    data = f.read(size)
    if len(data) != size:
        raise EOFError(f"expected {size} bytes, got {len(data)}")
    return struct.unpack(fmt, data)

def skip_position_array(f):
    (count,) = read_struct(f, "<i")
    if count:
        f.read(count * 12)  # count * BSPVector (3 floats)

class Vec3:
    __slots__ = ("x", "y", "z")
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x, self.y, self.z = x, y, z
    def __add__(self, o): return Vec3(self.x+o.x, self.y+o.y, self.z+o.z)
    def __sub__(self, o): return Vec3(self.x-o.x, self.y-o.y, self.z-o.z)
    def __mul__(self, s): return Vec3(self.x*s, self.y*s, self.z*s)
    def cross(self, o):
        return Vec3(self.y*o.z - self.z*o.y, self.z*o.x - self.x*o.z, self.x*o.y - self.y*o.x)
    def dot(self, o):
        return self.x*o.x + self.y*o.y + self.z*o.z
    def length(self):
        return (self.x**2 + self.y**2 + self.z**2) ** 0.5
    def normalized(self):
        n = self.length()
        if n < 1e-8:
            return Vec3(0.0, 1.0, 0.0)
        return Vec3(self.x/n, self.y/n, self.z/n)

# ── polygon triangulation: ear clipping, not a naive fan-from-vertex-0 ──
# idTech3-compiled BSPs only ever emit convex POLYGON faces (the compiler
# splits anything concave during the BSP build), where a fan is exact and
# free. This .bsx export doesn't carry that guarantee — verified directly
# against the data: 7 faces (6- and even one 4-vertex "quad") produce
# near-zero-area sliver triangles with a fan, because their vertex order
# traces a concave/notched outline. Ear clipping handles that correctly;
# for the genuinely-convex majority it produces the same triangles a fan
# would, just at slightly more compute cost (irrelevant for a one-off
# offline conversion).
def polygon_normal_newell(points):
    nx = ny = nz = 0.0
    n = len(points)
    for i in range(n):
        p1, p2 = points[i], points[(i + 1) % n]
        nx += (p1.y - p2.y) * (p1.z + p2.z)
        ny += (p1.z - p2.z) * (p1.x + p2.x)
        nz += (p1.x - p2.x) * (p1.y + p2.y)
    return Vec3(nx, ny, nz)

def project_polygon_2d(points):
    normal = polygon_normal_newell(points).normalized()
    p0 = points[0]
    u = Vec3(1.0, 0.0, 0.0)
    for p in points[1:]:
        d = p - p0
        if d.length() > 1e-6:
            u = d.normalized()
            break
    v = normal.cross(u)
    return [((p - p0).dot(u), (p - p0).dot(v)) for p in points]

def cross2(o, a, b):
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

def point_in_tri2(p, a, b, c):
    d1, d2, d3 = cross2(a, b, p), cross2(b, c, p), cross2(c, a, p)
    has_neg = d1 < 0 or d2 < 0 or d3 < 0
    has_pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (has_neg and has_pos)

def ear_clip_indices(points2d):
    """Returns a list of (i,j,k) index triples into points2d. Falls back to
    a plain fan for anything it can't resolve (self-intersecting/degenerate
    input) rather than dropping the face's geometry entirely."""
    n = len(points2d)
    if n < 3:
        return []
    if n == 3:
        return [(0, 1, 2)]
    idx = list(range(n))
    signed_area2 = sum(points2d[i][0] * points2d[(i + 1) % n][1] -
                       points2d[(i + 1) % n][0] * points2d[i][1] for i in range(n))
    ccw = signed_area2 > 0
    tris = []
    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        m = len(idx)
        ear_found = False
        for k in range(m):
            ip, ic, inx = idx[(k - 1) % m], idx[k], idx[(k + 1) % m]
            a, b, c = points2d[ip], points2d[ic], points2d[inx]
            cr = cross2(a, b, c)
            if (ccw and cr <= 0) or (not ccw and cr >= 0):
                continue
            if any(j not in (ip, ic, inx) and point_in_tri2(points2d[j], a, b, c) for j in idx):
                continue
            tris.append((ip, ic, inx))
            idx.pop(k)
            ear_found = True
            break
        if not ear_found:
            break  # bail — self-intersecting input; use a fan for the rest
    if len(idx) >= 3:
        base = idx[0]
        for k in range(1, len(idx) - 1):
            tris.append((base, idx[k], idx[k + 1]))
    return tris

# BSPVertex (28 bytes): position(3f) textureCoord(2f) lightmapCoord(2f) — we
# only keep position + textureCoord, lightmapCoord is read and discarded.
BSPVERTEX_FMT = "<3f2f2f"
BSPVERTEX_SIZE = struct.calcsize(BSPVERTEX_FMT)

# BSPFace (104 bytes): 12 ints, then 12 floats, then 2 ints — see
# tmp/apocalyx/glbsp.h's BSPFace struct for the field-by-field breakdown.
BSPFACE_FMT = "<12i12f2i"
BSPFACE_SIZE = struct.calcsize(BSPFACE_FMT)
assert BSPFACE_SIZE == 104

FACE_POLYGON, FACE_PATCH, FACE_MESH, FACE_BILLBOARD = 1, 2, 3, 4

class Face:
    __slots__ = ("textureID", "effect", "type", "firstVertex", "vertexesCount",
                 "firstMeshVertex", "meshVertexesCount", "normal", "patchSize")
    def __init__(self, raw):
        (self.textureID, self.effect, self.type, self.firstVertex, self.vertexesCount,
         self.firstMeshVertex, self.meshVertexesCount, lightmapID,
         lmCorner0, lmCorner1, lmSize0, lmSize1,
         lmPosX, lmPosY, lmPosZ,
         lmVec0X, lmVec0Y, lmVec0Z, lmVec1X, lmVec1Y, lmVec1Z,
         nx, ny, nz,
         patch0, patch1) = raw
        self.normal = Vec3(nx, ny, nz)
        self.patchSize = (patch0, patch1)

# This .bsx's stored vertex order winds every polygon/mesh face backward
# relative to what this engine's CCW-front convention expects (verified:
# needing mat->double_sided = true to see the floor from above, for every
# face, not just a handful — a uniform mismatch, not a per-face data
# quirk). Swap the last two vertices (and their UVs) of every triangle here
# so both the winding AND the geometric normal written into VRTS
# (recomputed from this same p0,p1,p2 order) come out correct together,
# instead of disabling backface culling for the whole level as a band-aid.
def fix_winding(p0, p1, p2, uv0, uv1, uv2, texture_id):
    return (p0, p2, p1, uv0, uv2, uv1, texture_id)

def bezier_basis(t):
    return (1 - t) ** 2, 2 * t * (1 - t), t ** 2

def tessellate_patch(controls, subdiv):
    """controls: 3x3 grid of (pos:Vec3, uv:(u,v)), row-major. Returns
    (grid_pos, grid_uv) each (subdiv+1) x (subdiv+1), row-major."""
    grid_pos = []
    grid_uv = []
    for iv in range(subdiv + 1):
        v = iv / subdiv
        bv = bezier_basis(v)
        row_pos = []
        row_uv = []
        for iu in range(subdiv + 1):
            u = iu / subdiv
            bu = bezier_basis(u)
            px = py = pz = 0.0
            tu = tv = 0.0
            for row in range(3):
                for col in range(3):
                    w = bv[row] * bu[col]
                    pos, uv = controls[row * 3 + col]
                    px += pos.x * w; py += pos.y * w; pz += pos.z * w
                    tu += uv[0] * w; tv += uv[1] * w
            row_pos.append(Vec3(px, py, pz))
            row_uv.append((tu, tv))
        grid_pos.append(row_pos)
        grid_uv.append(row_uv)
    return grid_pos, grid_uv

def parse_and_triangulate(bsx_path, patch_subdiv=6):
    """Reads a .bsx and returns (tris, textures): tris is a flat list of
    (pos0,pos1,pos2, uv0,uv1,uv2, textureID) with winding already fixed and
    degenerate slivers already dropped; textures is the texture-name table
    (index by triangle's textureID). Shared by bspx_to_h3d.py and
    bspx_to_obj.py so both tools triangulate identically."""
    with open(bsx_path, "rb") as f:
        for _ in range(7):  # starting/medikit/food/armor/bullets/grenades/weapon
            skip_position_array(f)

        (vertexesCount,) = read_struct(f, "<i")
        vertexes = []  # (Vec3 pos, (u,v))
        for _ in range(vertexesCount):
            x, y, z, u, v, lu, lv = read_struct(f, BSPVERTEX_FMT)
            vertexes.append((Vec3(x, y, z), (u, v)))

        (meshVertexesCount,) = read_struct(f, "<i")
        meshVertexes = list(read_struct(f, f"<{meshVertexesCount}i")) if meshVertexesCount else []

        (facesCount,) = read_struct(f, "<i")
        faces = [Face(read_struct(f, BSPFACE_FMT)) for _ in range(facesCount)]

        (texturesCount,) = read_struct(f, "<i")
        textures = []
        for _ in range(texturesCount):
            name_raw, flags, contents = read_struct(f, "<64sii")
            name = name_raw.split(b"\0", 1)[0].decode("ascii", "replace")
            textures.append(name)

        (lightmapsCount,) = read_struct(f, "<i")
        # lightmaps are external image files, nothing to read from the
        # stream itself — and we're dropping lightmapping anyway (see the
        # module docstring)

    print(f"parsed: {vertexesCount} verts, {meshVertexesCount} meshverts, "
          f"{facesCount} faces, {texturesCount} textures, {lightmapsCount} lightmaps")

    # ── triangulate every face into a flat list of (p0,p1,p2, uv0,uv1,uv2, textureID) ──
    tris = []  # (pos0,pos1,pos2, uv0,uv1,uv2, textureID)
    skipped_billboards = 0
    skipped_sky = 0
    for face in faces:
        if face.textureID < 0 or face.textureID >= texturesCount:
            continue
        # sky brushes: a real Quake sky shader is meant to be replaced by an
        # actual skybox at render time, never drawn as opaque geometry — a
        # BSP city map is typically fully enclosed in one of these, so
        # keeping the face here means the camera ends up inside a giant
        # untextured box (this is what looked like a "flipped floor" —
        # actually the underside/inside of the sky brush filling the view)
        if "sky" in textures[face.textureID].lower():
            skipped_sky += 1
            continue
        if face.type == FACE_POLYGON or face.type == FACE_MESH:
            if face.type == FACE_POLYGON:
                # ear-clip over vertexes[firstVertex .. firstVertex+vertexesCount)
                # — not a fan; see the ear_clip_indices comment for why
                if face.vertexesCount < 3:
                    continue
                base = face.firstVertex
                poly_pos = [vertexes[base + k][0] for k in range(face.vertexesCount)]
                poly_uv = [vertexes[base + k][1] for k in range(face.vertexesCount)]
                poly_2d = project_polygon_2d(poly_pos)
                for (ia, ib, ic) in ear_clip_indices(poly_2d):
                    tris.append(fix_winding(poly_pos[ia], poly_pos[ib], poly_pos[ic],
                                poly_uv[ia], poly_uv[ib], poly_uv[ic],
                                face.textureID))
            else:
                # MESH: meshVertexes are indices relative to firstVertex, triangle list
                for i in range(0, face.meshVertexesCount, 3):
                    off = face.firstMeshVertex + i
                    if off + 2 >= len(meshVertexes):
                        break
                    i0 = face.firstVertex + meshVertexes[off]
                    i1 = face.firstVertex + meshVertexes[off + 1]
                    i2 = face.firstVertex + meshVertexes[off + 2]
                    tris.append(fix_winding(vertexes[i0][0], vertexes[i1][0], vertexes[i2][0],
                                vertexes[i0][1], vertexes[i1][1], vertexes[i2][1],
                                face.textureID))
        elif face.type == FACE_PATCH:
            width, height = face.patchSize
            if width < 3 or height < 3:
                continue
            widthCount = (width - 1) // 2
            heightCount = (height - 1) // 2
            for py in range(heightCount):
                for px in range(widthCount):
                    controls = []
                    for row in range(3):
                        for col in range(3):
                            idx = face.firstVertex + (py * 2 * width + px * 2) + row * width + col
                            controls.append(vertexes[idx])
                    grid_pos, grid_uv = tessellate_patch(controls, patch_subdiv)
                    for gy in range(patch_subdiv):
                        for gx in range(patch_subdiv):
                            p00, p10 = grid_pos[gy][gx], grid_pos[gy][gx + 1]
                            p01, p11 = grid_pos[gy + 1][gx], grid_pos[gy + 1][gx + 1]
                            u00, u10 = grid_uv[gy][gx], grid_uv[gy][gx + 1]
                            u01, u11 = grid_uv[gy + 1][gx], grid_uv[gy + 1][gx + 1]
                            tris.append((p00, p10, p11, u00, u10, u11, face.textureID))
                            tris.append((p00, p11, p01, u00, u11, u01, face.textureID))
        else:
            skipped_billboards += 1

    if skipped_billboards:
        print(f"skipped {skipped_billboards} billboard face(s) (not static geometry)")
    if skipped_sky:
        print(f"skipped {skipped_sky} sky-brush face(s) (rendered by the engine's own sky, not geometry)")

    # no sliver/degenerate filtering here — auto-detecting "bad" triangles
    # by shape heuristics risks dropping legitimate thin geometry (it did:
    # a flatness-only threshold caught the real spike triangle but also
    # took out 2 real faces). Convert faithfully; fix actual bad geometry
    # by hand in Blender via bspx_to_obj.py instead.
    print(f"triangulated: {len(tris)} triangles")

    return tris, textures


# Resolves each used textureID to an actual file under textures_root and
# copies it alongside out_path (same relative path the name already
# encodes, e.g. "textures/maxpayne/Brick52a") so a dir-of-output-relative
# texture lookup just works for whoever reads the result. Shared by the
# h3d and obj writers — both need the exact same texture files sitting next
# to their output, just referenced through a different material format.
#
# case-insensitive lookup table: the .bsx stores lowercased texture names
# ("textures/maxpayne/brick52a") but the actual files on disk are
# mixed-case ("Brick52a.jpg") — fine on Windows/original engine, not on a
# case-sensitive filesystem here, so index every file under textures_root
# by its lowercased relative path once
def resolve_textures(tris, textures, textures_root, out_path):
    disk_index = {}
    for root, _dirs, files in os.walk(textures_root):
        for fn in files:
            rel = os.path.relpath(os.path.join(root, fn), textures_root)
            disk_index[rel.replace(os.sep, "/").lower()] = rel

    # only resolve textures actually referenced by a surviving triangle —
    # skipped sky faces etc. shouldn't produce a spurious "not found" warning
    used_tex_ids = sorted({t[6] for t in tris})

    out_dir = os.path.dirname(os.path.abspath(out_path))
    resolved_tex = {}  # textureID -> relative path written into the output, or None
    for tid in used_tex_ids:
        name = textures[tid]
        found = None
        for ext in (".jpg", ".png", ".tga"):
            actual_rel = disk_index.get((name + ext).lower())
            if actual_rel:
                src = os.path.join(textures_root, actual_rel)
                found = actual_rel.replace(os.sep, "/")
                dst = os.path.join(out_dir, found)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if not os.path.exists(dst):
                    with open(src, "rb") as sf, open(dst, "wb") as df:
                        df.write(sf.read())
                break
        resolved_tex[tid] = found
        if not found:
            print(f"warning: texture '{name}' not found under {textures_root}, "
                  f"faces using it will render untextured")

    return used_tex_ids, resolved_tex


def convert(bsx_path, textures_root, out_path, patch_subdiv=6):
    tris, textures = parse_and_triangulate(bsx_path, patch_subdiv)
    used_tex_ids, resolved_tex = resolve_textures(tris, textures, textures_root, out_path)

    # ── group triangles by textureID into per-material buffers, one
    # material per texture actually used ──
    material_slot = {tid: i for i, tid in enumerate(used_tex_ids)}

    write_h3d(out_path, tris, textures, resolved_tex, used_tex_ids, material_slot)

def write_h3d(out_path, tris, textures, resolved_tex, used_tex_ids, material_slot):
    buf = bytearray()

    def w_u32(v): buf.extend(struct.pack("<I", v & 0xFFFFFFFF))
    def w_f32(v): buf.extend(struct.pack("<f", v))
    def w_cstr(s): buf.extend(s.encode("ascii", "replace")); buf.append(0)
    def w_u8(v): buf.append(v & 0xFF)

    # MeshFormat.hpp defines each tag as a hex u32 constant (e.g.
    # MESH_MAGIC = 0x4D455348) written via WriteUInt — a little-endian
    # integer write, so the literal ASCII "MESH" ends up byte-reversed on
    # disk ("HSEM"). Writing the 4 ASCII bytes forward (my first attempt)
    # produced a file the C++ loader's magic check rejects outright.
    def w_tag(ascii4):
        buf.extend(ascii4[::-1])

    def begin_chunk(chunk_id):
        w_tag(chunk_id)
        pos = len(buf)
        w_u32(0)  # length placeholder
        return pos

    def end_chunk(pos):
        length = len(buf) - (pos + 4)
        buf[pos:pos + 4] = struct.pack("<I", length)

    # header
    w_tag(b"MESH")
    w_u32(100)  # MESH_VERSION

    # MATS chunk
    p = begin_chunk(b"MATS")
    w_u32(len(used_tex_ids))
    for tid in used_tex_ids:
        name = textures[tid] if tid < len(textures) else f"tex{tid}"
        w_cstr(os.path.basename(name))
        w_f32(1.0); w_f32(1.0); w_f32(1.0)  # diffuse tint
        w_f32(0.0); w_f32(0.0); w_f32(0.0)  # specular
        w_f32(32.0)  # shininess
        tex = resolved_tex.get(tid)
        if tex:
            w_u8(1)
            w_cstr(tex)
        else:
            w_u8(0)
    end_chunk(p)

    # one BUFF per material, unwelded (each triangle owns its 3 verts —
    # simple and correct; per-triangle geometric normals give flat shading
    # anyway, so there's nothing to gain from welding here)
    by_material = {}
    for tri in tris:
        by_material.setdefault(tri[6], []).append(tri)

    for tid in used_tex_ids:
        tris_here = by_material.get(tid, [])
        p = begin_chunk(b"BUFF")
        w_u32(material_slot[tid])
        w_u32(0)  # flags: unskinned

        pv = begin_chunk(b"VRTS")
        w_u32(len(tris_here) * 3)
        for (p0, p1, p2, uv0, uv1, uv2, _tid) in tris_here:
            n = (p1 - p0).cross(p2 - p0).normalized()
            for pos, uv in ((p0, uv0), (p1, uv1), (p2, uv2)):
                w_f32(pos.x); w_f32(pos.y); w_f32(pos.z)
                w_f32(n.x); w_f32(n.y); w_f32(n.z)
                w_f32(uv[0]); w_f32(uv[1])
        end_chunk(pv)

        pi = begin_chunk(b"IDXS")
        w_u32(len(tris_here) * 3)
        for i in range(len(tris_here) * 3):
            w_u32(i)
        end_chunk(pi)

        end_chunk(p)

    with open(out_path, "wb") as f:
        f.write(buf)
    print(f"wrote {out_path} ({len(buf)} bytes, {len(used_tex_ids)} materials)")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <map.bsx> <textures_dir> <out.h3d>")
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2], sys.argv[3])
