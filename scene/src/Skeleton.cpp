#include "scene/Skeleton.hpp"
#include <vector>

int Skeleton::find_bone(const char* name) const
{
    for (size_t i = 0; i < m_bones.size(); ++i)
        if (m_bones[i].name == name) return (int)i;
    return -1;
}

// bind-time TRS of every bone: the rest pose, and the base the blender
// starts from. Rotation goes matrix->quaternion directly (Shepperd) — an
// Euler round-trip corrupts orientations near gimbal, which showed up as
// stretched fingers on bones a clip leaves untracked.
void Skeleton::bind_pose(LocalPose* out) const
{
    for (size_t i = 0; i < m_bones.size(); ++i)
    {
        const Mat4& m = m_bones[i].bindLocal;
        LocalPose& p = out[i];
        p.position = Vec3(m.c[3][0], m.c[3][1], m.c[3][2]);
        p.scale =
            Vec3(sqrtf(m.c[0][0] * m.c[0][0] + m.c[0][1] * m.c[0][1] + m.c[0][2] * m.c[0][2]),
                 sqrtf(m.c[1][0] * m.c[1][0] + m.c[1][1] * m.c[1][1] + m.c[1][2] * m.c[1][2]),
                 sqrtf(m.c[2][0] * m.c[2][0] + m.c[2][1] * m.c[2][1] + m.c[2][2] * m.c[2][2]));
        const float sx = p.scale.x > 1e-8f ? 1.f / p.scale.x : 0.f;
        const float sy = p.scale.y > 1e-8f ? 1.f / p.scale.y : 0.f;
        const float sz = p.scale.z > 1e-8f ? 1.f / p.scale.z : 0.f;
        // r[row][col] from column-major storage, scale normalized out
        const float r[3][3] = {
            {m.c[0][0] * sx, m.c[1][0] * sy, m.c[2][0] * sz},
            {m.c[0][1] * sx, m.c[1][1] * sy, m.c[2][1] * sz},
            {m.c[0][2] * sx, m.c[1][2] * sy, m.c[2][2] * sz},
        };
        p.rotation = Quaternion::FromRotation(r);
    }
}

// single forward pass: the exporter guarantees parents precede children.
// global = parentGlobal * T*R*S; skinning matrix = global * inverseBind.
void Skeleton::evaluate(const LocalPose* locals, Mat4* globals, Mat4* palette) const
{
    // when the caller only wants the palette we still need globals as
    // intermediates — a fixed buffer covers any real skeleton
    Mat4 stackGlobals[256];
    Mat4* g = globals ? globals : stackGlobals;

    // m_order guarantees parents are computed before children, whatever
    // order the file stored the bones in (Sinbad's are alphabetical —
    // evaluating in file order lagged fingers by one frame)
    for (gl::u16 idx : m_order)
    {
        const size_t i = idx;
        const LocalPose& p = locals[i];
        Mat4 local = Mat4::Translate(p.position) * Mat4::Rotate(p.rotation) *
                     Mat4::Scale(p.scale.x, p.scale.y, p.scale.z);
        const gl::i32 parent = m_bones[i].parent;
        g[i] = (parent >= 0) ? g[parent] * local : local;
        if (palette) palette[i] = g[i] * m_bones[i].inverseBind;
    }
}

// depth-first from the roots: every bone lands after its parent
void Skeleton::finalize()
{
    const size_t n = m_bones.size();
    m_order.clear();
    m_order.reserve(n);
    std::vector<bool> placed(n, false);
    // roots first, then repeatedly place bones whose parent is placed —
    // n passes worst case, tiny arrays, runs once at load
    bool progress = true;
    while (m_order.size() < n && progress)
    {
        progress = false;
        for (size_t i = 0; i < n; ++i)
        {
            if (placed[i]) continue;
            const gl::i32 parent = m_bones[i].parent;
            if (parent < 0 || placed[(size_t)parent])
            {
                m_order.push_back((gl::u16)i);
                placed[i] = true;
                progress = true;
            }
        }
    }
    // cycles/orphans (corrupt file): append so nothing is skipped
    for (size_t i = 0; i < n; ++i)
        if (!placed[i]) m_order.push_back((gl::u16)i);
}

void Skeleton::add_bone(const char* name, gl::i32 parent, const Mat4& bindLocal,
                        const Mat4& inverseBind)
{
    Bone b;
    b.name = name;
    b.parent = parent;
    b.bindLocal = bindLocal;
    b.inverseBind = inverseBind;
    m_bones.push_back(std::move(b));
}
