#include "scene/Skeleton.hpp"

// TODO(evening): evaluate() forward pass + palette; port order guarantees
// from the exporter (parents precede children).

int Skeleton::find_bone(const char* name) const
{
    for (size_t i = 0; i < m_bones.size(); ++i)
        if (m_bones[i].name == name) return (int)i;
    return -1;
}

void Skeleton::bind_pose(LocalPose* out) const
{
    (void)out; // TODO(evening): decompose bindLocal into TRS
}

void Skeleton::evaluate(const LocalPose* locals, Mat4* globals, Mat4* palette) const
{
    (void)locals;
    (void)globals;
    (void)palette; // TODO(evening)
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
