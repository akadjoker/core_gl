#pragma once
#include <vector>
#include <string>
#include <cstdint>

using u8 = uint8_t;
using u32 = uint32_t;
using s32 = int32_t;

struct Vec3
{
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
};

struct Mat4
{
    float m[16];
    Mat4()
    {
        for (int i = 0; i < 16; i++)
            m[i] = 0.0f;
    }
    float at(int row, int col)
    {
        return m[col * 4 + row];
    }
};

struct Quat
{
    float x, y, z, w;
    Quat() : x(0), y(0), z(0), w(1) {}
    Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
};

struct Keyframe
{
    float time;
    Vec3 position;
    Quat rotation;
    Vec3 scale;

    Keyframe() : time(0), scale(1.0f, 1.0f, 1.0f) {}
};

struct AnimationChannel
{
    std::string boneName;
    std::vector<Keyframe> keyframes;
};

struct SimpleAnimation
{
    std::string name;
    float duration;
    float ticksPerSecond;
    std::vector<AnimationChannel> channels;
};

struct Vertex
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

struct VertexSkin
{
    u8 boneIDs[4];
    float weights[4];
};
 
struct Bone
{
    std::string name;
    s32 parentIndex;
    Mat4 localTransform;
    Mat4 inverseBindPose;
};

struct SimpleMaterial
{
    std::string name;
    Vec3 diffuse;
    Vec3 specular;
    float shininess;

    std::string textures[8];
    u8 textureCount;

    SimpleMaterial()
        : diffuse(0.8f, 0.8f, 0.8f), specular(0.5f, 0.5f, 0.5f), shininess(32.0f), textureCount(0)
    {
        for (int i = 0; i < 8; i++)
            textures[i] = "";
    }
};

struct AnimationInfo
{
    char name[64];
    float duration;
    float ticksPerSecond;
    u32 numChannels;
};

class SimpleMeshBuffer
{
public:
    SimpleMeshBuffer() : m_materialIndex(0), m_skinned(false) {}

    void SetMaterialIndex(u32 index) { m_materialIndex = index; }
    u32 GetMaterialIndex() const { return m_materialIndex; }

    void AddVertex(const Vertex &v) { m_vertices.push_back(v); }
    void AddVertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
    {
        Vertex vert = {x, y, z, nx, ny, nz, u, v};
        m_vertices.push_back(vert);
    }

    void AddFace(u32 i0, u32 i1, u32 i2)
    {
        m_indices.push_back(i0);
        m_indices.push_back(i1);
        m_indices.push_back(i2);
    }

    void SetSkinData(const std::vector<VertexSkin> &data)
    {
        m_skinData = data;
        m_skinned = !data.empty();
    }

    bool IsSkinned() const { return m_skinned; }

    u32 GetVertexCount() const { return m_vertices.size(); }
    u32 GetIndexCount() const { return m_indices.size(); }

    const Vertex *GetVertices() const { return m_vertices.data(); }
    const u32 *GetIndices() const { return m_indices.data(); }
    const VertexSkin *GetSkinData() const { return m_skinData.data(); }
    

private:
    std::vector<Vertex> m_vertices;
    std::vector<u32> m_indices;
    std::vector<VertexSkin> m_skinData;
    u32 m_materialIndex;
    bool m_skinned;
};

class SimpleMesh
{
public:
    SimpleMaterial *AddMaterial(const std::string &name)
    {
        SimpleMaterial mat;
        mat.name = name;
        mat.diffuse = Vec3(0.8f, 0.8f, 0.8f);
        mat.specular = Vec3(0.5f, 0.5f, 0.5f);
        mat.shininess = 32.0f;
        mat.textureCount = 0;  

        for (int i = 0; i < 8; i++)
            mat.textures[i] = "";  

        m_materials.push_back(mat);
        return &m_materials.back();
   
    }

    SimpleMeshBuffer *AddBuffer(u32 materialIndex)
    {
        SimpleMeshBuffer buffer;
        buffer.SetMaterialIndex(materialIndex);
        m_buffers.push_back(buffer);
        return &m_buffers.back();
    }

 
    u32 GetMaterialCount() const { return m_materials.size(); }
    u32 GetBufferCount() const { return m_buffers.size(); }
   

    SimpleMaterial *GetMaterial(u32 index) { return &m_materials[index]; }
    SimpleMeshBuffer *GetBuffer(u32 index) { return &m_buffers[index]; }
 

    bool HasSkeleton() const { return !bones.empty(); }

    std::vector<Bone> bones;
private:
    std::vector<SimpleMaterial> m_materials;
    std::vector<SimpleMeshBuffer> m_buffers;
};