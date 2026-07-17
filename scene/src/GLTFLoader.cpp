// glTF 2.0 skinned-mesh loader, implemented as SkinnedMesh::load_gltf()/
// load_animations_gltf() — dispatched into transparently from
// SkinnedMesh::load()/load_animations() by sniffing the file's first bytes
// (see SkinnedMesh.cpp), so every existing call site (AssetManager::
// loadSkinnedMesh/loadAnimation, every character demo) works unchanged
// whether the file is the native .mesh/.anim format or glTF.
//
// (same cgltf-based approach —
// first skinned node's mesh+skin, bone hierarchy from skin->joints,
// per-channel keyframes) onto the current engine's actual types, which
// differ from that reference in one structural way: AnimationClip's
// BoneTrack has ONE shared `times` array driving position/rotation/scale
// in lockstep (scene/src/AnimationClip.cpp always indexes all three with
// the same k0/k1), whereas glTF gives each TRS channel its own independent
// time array and any channel may be absent for a given bone. This loader
// resamples onto a unified per-bone time array (the union of all its
// channels' keyframe times) before building the BoneTrack.
//
// Materials are intentionally NOT built here: SkinnedMesh has nowhere to
// store them (no material field at all), and SkinnedMeshInstance only
// takes a single whole-instance Material* (scene/include/scene/
// SkinnedMeshInstance.hpp) — exactly like the existing Sinbad demo, the
// caller loads its own texture and assigns a Material after loading. glTF
// PBR material data isn't plumbed through; out of scope for the same
// reason (matches tmp/core's own reference, which also only used the
// first material's base_color and nothing else).

#include "scene/AnimationClip.hpp"
#include "scene/Filesystem.hpp"
#include "scene/Skeleton.hpp"
#include "scene/SkinnedMesh.hpp"
#include "cgltf.h"
#include <coregl/gl_log.hpp>
#include <algorithm>
#include <unordered_map>

namespace
{
gl::u8 clampBone(cgltf_uint v, cgltf_size njoints)
{
    cgltf_uint maxv = njoints > 0 ? (cgltf_uint)njoints - 1u : 0u;
    if (v > maxv) v = maxv;
    return (gl::u8)v;
}

// cgltf_parse_file/cgltf_load_buffers do their own raw file I/O — they
// never go through fs::Filesystem, so a relative path only works if it
// happens to resolve against the process's actual working directory.
// Every native loader in this engine sidesteps that by reading through
// fs::getFilesystem() (whose registered search folders — see
// demos/demo_app.hpp's addFolder(".")/("..")/("../..") — already tolerate
// running from a few different working directories); cgltf gets none of
// that for free, so resolve the path through the same Filesystem first.
std::string resolveGltfPath(const char* path)
{
    char buf[1024];
    if (fs::getFilesystem().resolvePath(path, buf, sizeof(buf))) return buf;
    return path; // fall back to the original; cgltf's own error names it
}

// T * R * S, matching the engine's own convention (see BoneAttachment.cpp:
// Mat4::Translate(...) * Mat4::Rotate(...))
Mat4 nodeLocalMatrix(const cgltf_node& n)
{
    if (n.has_matrix) return Mat4(n.matrix); // column-major flat copy, no transpose needed
    Vec3 t = n.has_translation ? Vec3(n.translation[0], n.translation[1], n.translation[2])
                               : Vec3(0.f, 0.f, 0.f);
    Quaternion r = n.has_rotation
                       ? Quaternion(n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3])
                       : Quaternion::Identity();
    Vec3 s = n.has_scale ? Vec3(n.scale[0], n.scale[1], n.scale[2]) : Vec3(1.f, 1.f, 1.f);
    return Mat4::Translate(t) * Mat4(r) * Mat4::Scale(s.x, s.y, s.z);
}

// linear search for the surrounding pair + lerp/nlerp — same scheme as
// AnimationClip::sample(), just operating on ONE glTF channel's own
// independent time array instead of a whole BoneTrack.
Vec3 sampleVec3Channel(const std::vector<float>& t, const std::vector<Vec3>& v, float time,
                       const Vec3& fallback)
{
    if (t.empty()) return fallback;
    if (t.size() == 1 || time <= t.front()) return v.front();
    if (time >= t.back()) return v.back();
    size_t k1 = (size_t)(std::upper_bound(t.begin(), t.end(), time) - t.begin());
    size_t k0 = k1 - 1;
    float span = t[k1] - t[k0];
    float f = span > 1e-6f ? (time - t[k0]) / span : 0.f;
    return v[k0] + (v[k1] - v[k0]) * f;
}

Quaternion sampleQuatChannel(const std::vector<float>& t, const std::vector<Quaternion>& v,
                            float time, const Quaternion& fallback)
{
    if (t.empty()) return fallback;
    if (t.size() == 1 || time <= t.front()) return v.front();
    if (time >= t.back()) return v.back();
    size_t k1 = (size_t)(std::upper_bound(t.begin(), t.end(), time) - t.begin());
    size_t k0 = k1 - 1;
    float span = t[k1] - t[k0];
    float f = span > 1e-6f ? (time - t[k0]) / span : 0.f;
    return Quaternion::Nlerp(v[k0], v[k1], f);
}

// raw per-channel glTF keyframes for one bone, before resampling
struct RawBoneChannels
{
    std::vector<float> posT;
    std::vector<Vec3> posV;
    std::vector<float> rotT;
    std::vector<Quaternion> rotV;
    std::vector<float> sclT;
    std::vector<Vec3> sclV;
};

// builds one clip's worth of BoneTracks from a cgltf_animation, binding
// channels to bones via `boneOf` (already-loaded skeleton's node->index
// map) and resampling onto each bone's own unified time array.
void buildClipTracks(const cgltf_animation& src, const std::unordered_map<const cgltf_node*, int>& boneOf,
                     const Skeleton& skel, const std::vector<LocalPose>& bindPose,
                     AnimationClip& outClip)
{
    std::unordered_map<int, RawBoneChannels> perBone;

    for (cgltf_size ci = 0; ci < src.channels_count; ++ci)
    {
        const cgltf_animation_channel& ch = src.channels[ci];
        if (!ch.sampler || !ch.target_node) continue;
        auto it = boneOf.find(ch.target_node);
        if (it == boneOf.end()) continue; // channel targets a node outside this skeleton
        int boneIdx = it->second;

        cgltf_accessor *ta = ch.sampler->input, *va = ch.sampler->output;
        if (!ta || !va) continue;
        cgltf_size kc = std::min(ta->count, va->count);
        RawBoneChannels& rc = perBone[boneIdx];

        for (cgltf_size ki = 0; ki < kc; ++ki)
        {
            float t = 0.f;
            cgltf_accessor_read_float(ta, ki, &t, 1);
            if (ch.target_path == cgltf_animation_path_type_translation)
            {
                float v[3];
                cgltf_accessor_read_float(va, ki, v, 3);
                rc.posT.push_back(t);
                rc.posV.push_back(Vec3(v[0], v[1], v[2]));
            }
            else if (ch.target_path == cgltf_animation_path_type_rotation)
            {
                float v[4];
                cgltf_accessor_read_float(va, ki, v, 4);
                Quaternion q(v[0], v[1], v[2], v[3]);
                q.normalize();
                rc.rotT.push_back(t);
                rc.rotV.push_back(q);
            }
            else if (ch.target_path == cgltf_animation_path_type_scale)
            {
                float v[3];
                cgltf_accessor_read_float(va, ki, v, 3);
                rc.sclT.push_back(t);
                rc.sclV.push_back(Vec3(v[0], v[1], v[2]));
            }
            // weights (morph targets) channel: not applicable, no track to fill
        }
    }

    float clipDuration = 0.f;
    for (auto& kv : perBone)
    {
        int boneIdx = kv.first;
        RawBoneChannels& rc = kv.second;

        // unified time array = sorted, de-duplicated union of whichever
        // channels this bone actually has
        std::vector<float> unified;
        unified.reserve(rc.posT.size() + rc.rotT.size() + rc.sclT.size());
        unified.insert(unified.end(), rc.posT.begin(), rc.posT.end());
        unified.insert(unified.end(), rc.rotT.begin(), rc.rotT.end());
        unified.insert(unified.end(), rc.sclT.begin(), rc.sclT.end());
        if (unified.empty()) continue;
        std::sort(unified.begin(), unified.end());
        unified.erase(std::unique(unified.begin(), unified.end(),
                                  [](float a, float b) { return fabsf(a - b) < 1e-6f; }),
                      unified.end());

        const LocalPose& bind = bindPose[(size_t)boneIdx];

        BoneTrack tr;
        tr.bone = boneIdx;
        tr.times = unified;
        tr.positions.reserve(unified.size());
        tr.rotations.reserve(unified.size());
        tr.scales.reserve(unified.size());
        for (float t : unified)
        {
            tr.positions.push_back(sampleVec3Channel(rc.posT, rc.posV, t, bind.position));
            tr.rotations.push_back(sampleQuatChannel(rc.rotT, rc.rotV, t, bind.rotation));
            tr.scales.push_back(sampleVec3Channel(rc.sclT, rc.sclV, t, bind.scale));
        }
        clipDuration = std::max(clipDuration, unified.back());
        outClip.tracks().push_back(std::move(tr));
    }
    outClip.set_duration(clipDuration > 0.f ? clipDuration : 1.f);
    (void)skel;
}
} // namespace

bool SkinnedMesh::load_gltf(const char* path)
{
    std::string resolved = resolveGltfPath(path);
    cgltf_options opts = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, resolved.c_str(), &data) != cgltf_result_success)
    {
        gl::Log::Error("[GLTF] parse failed: %s", path);
        return false;
    }
    if (cgltf_load_buffers(&opts, data, resolved.c_str()) != cgltf_result_success)
    {
        gl::Log::Error("[GLTF] load_buffers failed: %s", path);
        cgltf_free(data);
        return false;
    }

    // first node with a mesh + skin
    const cgltf_skin* skin = nullptr;
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
        if (data->nodes[i].mesh && data->nodes[i].skin && data->nodes[i].skin->joints_count > 0)
        {
            skin = data->nodes[i].skin;
            break;
        }
    if (!skin)
    {
        gl::Log::Error("[GLTF] no skinned node: %s", path);
        cgltf_free(data);
        return false;
    }

    // ── skeleton: bind-time local transform + inverse bind + parent ──
    std::unordered_map<const cgltf_node*, int> boneOf;
    for (cgltf_size i = 0; i < skin->joints_count; ++i) boneOf[skin->joints[i]] = (int)i;

    for (cgltf_size i = 0; i < skin->joints_count; ++i)
    {
        const cgltf_node* j = skin->joints[i];
        const char* name = (j && j->name) ? j->name : nullptr;
        std::string fallbackName = "joint_" + std::to_string(i);
        gl::i32 parent = -1;
        if (j && j->parent)
        {
            auto it = boneOf.find(j->parent);
            if (it != boneOf.end()) parent = (gl::i32)it->second;
        }

        Mat4 inverseBind = Mat4::Identity();
        if (skin->inverse_bind_matrices && i < skin->inverse_bind_matrices->count)
        {
            float ibm[16];
            cgltf_accessor_read_float(skin->inverse_bind_matrices, i, ibm, 16);
            inverseBind = Mat4(ibm);
        }

        m_skeleton.add_bone(name ? name : fallbackName.c_str(), parent, nodeLocalMatrix(*j),
                            inverseBind);
    }
    m_skeleton.finalize();

    // ── geometry: every primitive of every node sharing this skin ──
    std::vector<MeshVertex> verts;
    std::vector<gl::u32> indices;
    m_weights.clear();

    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni)
    {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh || node.skin != skin) continue;

        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi)
        {
            const cgltf_primitive& prim = node.mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            cgltf_accessor *pos = nullptr, *nor = nullptr, *uv = nullptr;
            cgltf_accessor *jnt = nullptr, *wgt = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
            {
                const cgltf_attribute& a = prim.attributes[ai];
                if (a.type == cgltf_attribute_type_position) pos = a.data;
                else if (a.type == cgltf_attribute_type_normal) nor = a.data;
                else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
                else if (a.type == cgltf_attribute_type_joints && a.index == 0) jnt = a.data;
                else if (a.type == cgltf_attribute_type_weights && a.index == 0) wgt = a.data;
            }
            if (!pos) continue;

            const gl::u32 vbase = (gl::u32)verts.size();
            const gl::u32 firstIndex = (gl::u32)indices.size();
            const cgltf_size vcount = pos->count;
            verts.resize(vbase + vcount);
            m_weights.resize(vbase + vcount);
            Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);

            for (cgltf_size vi = 0; vi < vcount; ++vi)
            {
                MeshVertex& v = verts[vbase + vi];
                float f[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(pos, vi, f, 3);
                v.position = Vec3(f[0], f[1], f[2]);
                v.normal = Vec3(0, 1, 0);
                if (nor)
                {
                    cgltf_accessor_read_float(nor, vi, f, 3);
                    v.normal = Vec3(f[0], f[1], f[2]);
                }
                v.tangent = Vec4(1, 0, 0, 1); // recomputed below via compute_tangents()
                v.uv = Vec2(0, 0);
                if (uv)
                {
                    cgltf_accessor_read_float(uv, vi, f, 2);
                    v.uv = Vec2(f[0], f[1]);
                }
                mn = mn.Min(v.position);
                mx = mx.Max(v.position);

                VertexWeights& w = m_weights[vbase + vi];
                if (jnt && wgt)
                {
                    cgltf_uint ji[4] = {0, 0, 0, 0};
                    float wv[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(jnt, vi, ji, 4);
                    cgltf_accessor_read_float(wgt, vi, wv, 4);
                    for (int k = 0; k < 4; ++k) w.bone[k] = clampBone(ji[k], skin->joints_count);
                    float s = wv[0] + wv[1] + wv[2] + wv[3];
                    if (s > 1e-6f)
                        for (int k = 0; k < 4; ++k) w.weight[k] = wv[k] / s;
                    else
                    {
                        w.weight[0] = 1.f;
                        w.weight[1] = w.weight[2] = w.weight[3] = 0.f;
                    }
                }
                else
                {
                    w.bone[0] = w.bone[1] = w.bone[2] = w.bone[3] = 0;
                    w.weight[0] = 1.f;
                    w.weight[1] = w.weight[2] = w.weight[3] = 0.f;
                }
            }

            if (prim.indices)
                for (cgltf_size ii = 0; ii < prim.indices->count; ++ii)
                    indices.push_back(vbase + (gl::u32)cgltf_accessor_read_index(prim.indices, ii));
            else
                for (cgltf_size vi = 0; vi < vcount; ++vi) indices.push_back(vbase + (gl::u32)vi);

            int materialSlot = prim.material ? (int)(prim.material - data->materials) : 0;
            m_mesh.add_surface(firstIndex, (gl::u32)indices.size() - firstIndex, materialSlot,
                              BoundingBox(mn, mx));
        }
    }

    if (verts.empty() || indices.empty() || m_skeleton.empty() || m_weights.size() != verts.size())
    {
        gl::Log::Error("[GLTF] '%s' — no usable skinned geometry", path);
        cgltf_free(data);
        return false;
    }

    std::vector<Surface> surfaces = m_mesh.surfaces();
    std::sort(surfaces.begin(), surfaces.end(),
             [](const Surface& a, const Surface& b) { return a.material_slot < b.material_slot; });
    m_mesh.set_data(verts.data(), (gl::u32)verts.size(), indices.data(), (gl::u32)indices.size());
    for (const Surface& s : surfaces) m_mesh.add_surface(s.first_index, s.index_count, s.material_slot, s.bounds);
    m_mesh.compute_tangents();

    // ── animations embedded in this same file, if any ──
    if (data->animations_count > 0)
    {
        std::vector<LocalPose> bindPose((size_t)m_skeleton.bone_count());
        m_skeleton.bind_pose(bindPose.data());

        for (cgltf_size ai = 0; ai < data->animations_count; ++ai)
        {
            const cgltf_animation& src = data->animations[ai];
            AnimationClip* clip = new AnimationClip();
            clip->set_name((src.name && src.name[0]) ? src.name : ("anim_" + std::to_string(ai)));
            buildClipTracks(src, boneOf, m_skeleton, bindPose, *clip);
            if (clip->tracks().empty())
            {
                delete clip;
                continue;
            }
            gl::Log::Info("[GLTF]   anim '%s' dur=%.3f tracks=%zu", clip->name().c_str(),
                         clip->duration(), clip->tracks().size());
            m_clips.push_back(clip);
        }
    }

    gl::Log::Info("[GLTF] '%s' — %u verts, %u indices, %d bones, %zu anims", path,
                 (gl::u32)verts.size(), (gl::u32)indices.size(), m_skeleton.bone_count(),
                 m_clips.size());
    cgltf_free(data);
    m_loaded = true;
    return true;
}

// anim-only file (e.g. idle.glb: channels but no mesh/skin of its own) —
// binds to the ALREADY-loaded skeleton by bone name, same contract as the
// native format's load_animations().
bool SkinnedMesh::load_animations_gltf(const char* path)
{
    if (m_skeleton.empty())
    {
        gl::Log::Error("[GLTF] load_animations_gltf('%s') called before a skeleton is loaded", path);
        return false;
    }

    std::string resolved = resolveGltfPath(path);
    cgltf_options opts = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, resolved.c_str(), &data) != cgltf_result_success)
    {
        gl::Log::Error("[GLTF] parse failed: %s", path);
        return false;
    }
    if (cgltf_load_buffers(&opts, data, resolved.c_str()) != cgltf_result_success)
    {
        gl::Log::Error("[GLTF] load_buffers failed: %s", path);
        cgltf_free(data);
        return false;
    }
    if (data->animations_count == 0)
    {
        gl::Log::Error("[GLTF] '%s' has no animations", path);
        cgltf_free(data);
        return false;
    }

    // channels target nodes by pointer, and this file's own cgltf_data has
    // its OWN node objects (different pointers than the mesh file's) — so
    // rebind by NAME against the already-loaded skeleton instead of reusing
    // a node-pointer map from load_gltf().
    std::unordered_map<const cgltf_node*, int> boneOf;
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        const cgltf_node& n = data->nodes[i];
        if (!n.name) continue;
        int idx = m_skeleton.find_bone(n.name);
        if (idx >= 0) boneOf[&n] = idx;
    }

    std::vector<LocalPose> bindPose((size_t)m_skeleton.bone_count());
    m_skeleton.bind_pose(bindPose.data());

    int loaded = 0;
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai)
    {
        const cgltf_animation& src = data->animations[ai];
        AnimationClip* clip = new AnimationClip();
        clip->set_name((src.name && src.name[0]) ? src.name : ("anim_" + std::to_string(ai)));
        buildClipTracks(src, boneOf, m_skeleton, bindPose, *clip);
        if (clip->tracks().empty())
        {
            delete clip;
            continue;
        }
        gl::Log::Info("[GLTF]   anim '%s' dur=%.3f tracks=%zu", clip->name().c_str(),
                     clip->duration(), clip->tracks().size());
        m_clips.push_back(clip);
        ++loaded;
    }

    cgltf_free(data);
    if (loaded == 0)
    {
        gl::Log::Error("[GLTF] '%s' — no channel targeted a bone in the loaded skeleton", path);
        return false;
    }
    return true;
}
