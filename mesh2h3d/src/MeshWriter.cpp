
#include "MeshWriter.hpp"
#include "Stream.hpp"
#include "MeshFormat.hpp"
#include <iostream>

MeshWriter::MeshWriter()
    : m_stream(nullptr)
{
}

MeshWriter::~MeshWriter()
{
}

bool MeshWriter::Save(  SimpleMesh* mesh, const std::string& filename)
{
    if (!mesh)
    {
        std::cerr << "[MeshWriter] Invalid mesh pointer" << std::endl;
        return false;
    }
    
    FileStream stream;
    if (!stream.Open(filename, "wb"))
    {
        std::cerr << "[MeshWriter] Failed to open: " << filename << std::endl;
        return false;
    }
    
    m_stream = &stream;
    m_stream->SetBigEndian(false); // Little-endian
    
    // Magic + Version
    m_stream->WriteUInt(MESH_MAGIC);
    m_stream->WriteUInt(MESH_VERSION);
    
    // Materials
 
    WriteMaterialsChunk(mesh);
    
    
    // Skeleton (se existir)
    if (mesh->HasSkeleton())
    {
        WriteSkeletonChunk(mesh);
    }
    
    // Buffers
    for (u32 i = 0; i < mesh->GetBufferCount(); i++)
    {
        WriteBufferChunk(mesh->GetBuffer(i));
    }
    
    
    std::cout << "[MeshWriter] Saved: " << mesh->GetBufferCount() << " buffers, "
    << mesh->GetMaterialCount() << " materials";
    
    if (mesh->HasSkeleton())
    std::cout << ", " << mesh->bones.size() << " bones";
    
    stream.Close();
    std::cout << std::endl;
    
    return true;
}

void MeshWriter::BeginChunk(u32 chunkId, long* posOut)
{
    m_stream->WriteUInt(chunkId);
    m_stream->WriteUInt(0); // Placeholder para length
    *posOut = m_stream->Tell();
}

void MeshWriter::EndChunk(long startPos)
{
    long currentPos = m_stream->Tell();
    u32 length = static_cast<u32>(currentPos - startPos);
    m_stream->Seek(startPos - 4, SeekOrigin::Begin);
    m_stream->WriteUInt(length);
    m_stream->Seek(currentPos, SeekOrigin::Begin);
}

void MeshWriter::WriteMaterialsChunk(  SimpleMesh* mesh)
{
    long startPos;
    BeginChunk(CHUNK_MATS, &startPos);
    
    u32 numMaterials = mesh->GetMaterialCount();
    m_stream->WriteUInt(numMaterials);
    
    for (u32 i = 0; i < numMaterials; i++)
    {
        const SimpleMaterial* mat = mesh->GetMaterial(i);
        
        // Nome do material (null-terminated string)
        m_stream->WriteCString(mat->name);
        
        // Diffuse color
        m_stream->WriteFloat(mat->diffuse.x);
        m_stream->WriteFloat(mat->diffuse.y);
        m_stream->WriteFloat(mat->diffuse.z);
        
        // Specular color
        m_stream->WriteFloat(mat->specular.x);
        m_stream->WriteFloat(mat->specular.y);
        m_stream->WriteFloat(mat->specular.z);
        
        // Shininess
        m_stream->WriteFloat(mat->shininess);

        m_stream->WriteByte(mat->textureCount);
        
        // Escreve cada textura
        for (u8 j = 0; j < mat->textureCount; j++)
        {
            m_stream->WriteCString(mat->textures[j]);
        }
        
 
        
    }
    
    EndChunk(startPos);
}


void PrintMatrix( Mat4 &mat)
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            std::cout << mat.at(i, j) << " ";
        }
        std::cout << std::endl;
    }
}

void MeshWriter::WriteSkeletonChunk(  SimpleMesh* mesh)
{
    long startPos;
    BeginChunk(CHUNK_SKEL, &startPos);
    
    u32 numBones = mesh->bones.size();
    m_stream->WriteUInt(numBones);
    
    for (u32 i = 0; i < numBones; i++)
    {
         Bone& bone = mesh->bones[i];
        
        // Nome do bone
        m_stream->WriteCString(bone.name);
        
        // Parent index (-1 para root)
        m_stream->WriteInt(bone.parentIndex);

     //   std::cout << "  [SKEL] Bone: " << bone.name << ", Parent: " << bone.parentIndex << std::endl;
        
        // Local transform matrix (16 floats)
        for (int j = 0; j < 16; j++)
        {
            m_stream->WriteFloat(bone.localTransform.m[j]);
        }
     //   PrintMatrix(bone.localTransform);
        
        // Inverse bind pose matrix (16 floats)
        for (int j = 0; j < 16; j++)
        {
            m_stream->WriteFloat(bone.inverseBindPose.m[j]);
        }
     //   PrintMatrix(bone.inverseBindPose);
    }
    
    EndChunk(startPos);
}

void MeshWriter::WriteBufferChunk(  SimpleMeshBuffer* buffer)
{
    long startPos;
    BeginChunk(CHUNK_BUFF, &startPos);
    
    m_stream->WriteUInt(buffer->GetMaterialIndex());
    
    u32 flags = buffer->IsSkinned() ? BUFFER_FLAG_SKINNED : 0;
    m_stream->WriteUInt(flags);
    
    // DEBUG
    std::cout << "  [BUFF] Material: " << buffer->GetMaterialIndex() 
              << ", Skinned: " << (buffer->IsSkinned() ? "YES" : "NO") 
              << std::endl;
    
    WriteVerticesChunk(buffer);
    WriteIndicesChunk(buffer);
    
    if (flags & BUFFER_FLAG_SKINNED)
    {
        std::cout << "  [BUFF] Writing skin data..." << std::endl;
        WriteSkinChunk(buffer);
    }
    else
    {
        std::cout << "  [BUFF] ⚠ No skin data to write!" << std::endl;
    }
    
    EndChunk(startPos);
}


void MeshWriter::WriteVerticesChunk(  SimpleMeshBuffer* buffer)
{
    long startPos;
    BeginChunk(CHUNK_VRTS, &startPos);
    
    u32 numVertices = buffer->GetVertexCount();
    m_stream->WriteUInt(numVertices);
    
    const Vertex* vertices = buffer->GetVertices();
    
    // Escreve todos os vértices (8 floats cada: pos + normal + uv)
    for (u32 i = 0; i < numVertices; i++)
    {
        // Position
        m_stream->WriteFloat(vertices[i].x);
        m_stream->WriteFloat(vertices[i].y);
        m_stream->WriteFloat(vertices[i].z);
        
        // Normal
        m_stream->WriteFloat(vertices[i].nx);
        m_stream->WriteFloat(vertices[i].ny);
        m_stream->WriteFloat(vertices[i].nz);
        
        // UV
        m_stream->WriteFloat(vertices[i].u);
        m_stream->WriteFloat(vertices[i].v);
    }
    
    EndChunk(startPos);
}

void MeshWriter::WriteIndicesChunk(  SimpleMeshBuffer* buffer)
{
    long startPos;
    BeginChunk(CHUNK_IDXS, &startPos);
    
    u32 numIndices = buffer->GetIndexCount();
    m_stream->WriteUInt(numIndices);
    
    const u32* indices = buffer->GetIndices() ;
    
    // Escreve todos os índices
    for (u32 i = 0; i < numIndices / 3 ; i++)
    {
        m_stream->WriteUInt(indices[i * 3]);
        m_stream->WriteUInt(indices[i * 3 + 1]);
        m_stream->WriteUInt(indices[i * 3 + 2]);
    }
    
    EndChunk(startPos);
}

void MeshWriter::WriteSkinChunk(  SimpleMeshBuffer* buffer)
{
    long startPos;
    BeginChunk(CHUNK_SKIN, &startPos);
    
    u32 numVertices = buffer->GetVertexCount();
    m_stream->WriteUInt(numVertices);
    
    const VertexSkin* skinData = buffer->GetSkinData();
    
    for (u32 i = 0; i < numVertices; i++)
    {
        // Bone IDs (4 bytes)
        for (int j = 0; j < 4; j++)
        {
            m_stream->WriteByte(skinData[i].boneIDs[j]);
        }
        
        // Weights (4 floats)
        for (int j = 0; j < 4; j++)
        {
            m_stream->WriteFloat(skinData[i].weights[j]);
        }
    }
    
    EndChunk(startPos);
}

AnimWriter::AnimWriter()
    : m_stream(nullptr)
{
}

AnimWriter::~AnimWriter()
{
}

bool AnimWriter::Save(const std::string& filename, const SimpleAnimation& animation)
{
    FileStream stream;
    if (!stream.Open(filename, "wb"))
    {
        std::cerr << "[AnimWriter] Failed to open: " << filename << std::endl;
        return false;
    }
    
    m_stream = &stream;
    m_stream->SetBigEndian(false);
    
    // Magic + Version
    m_stream->WriteUInt(ANIM_MAGIC);
    m_stream->WriteUInt(ANIM_VERSION);
    
    // Info chunk
    AnimationInfo info = {};
    strncpy(info.name, animation.name.c_str(), 63);
    info.name[63] = '\0';
    info.duration = animation.duration;
    info.ticksPerSecond = animation.ticksPerSecond;
    info.numChannels = animation.channels.size();
    
    WriteInfoChunk(info);
    
    // Channels
    for (const auto& channel : animation.channels)
    {
        WriteChannelChunk(channel);
    }
    
    stream.Close();
    
    std::cout << "[AnimWriter] Saved: " << animation.name 
              << " (" << animation.channels.size() << " channels, "
              << animation.duration << "s)" << std::endl;
    
    return true;
}



void AnimWriter::BeginChunk(u32 chunkId, long* posOut)
{
    m_stream->WriteUInt(chunkId);
    m_stream->WriteUInt(0); // Placeholder
    *posOut = m_stream->Tell();
}

void AnimWriter::EndChunk(long startPos)
{
    long currentPos = m_stream->Tell();
    u32 length = static_cast<u32>(currentPos - startPos);
    
    m_stream->Seek(startPos - 4, SeekOrigin::Begin);
    m_stream->WriteUInt(length);
    m_stream->Seek(currentPos, SeekOrigin::Begin);
}

void AnimWriter::WriteInfoChunk(const AnimationInfo& info)
{
    long startPos;
    BeginChunk(ANIM_CHUNK_INFO, &startPos);
    
    // Name (64 bytes fixed)
    m_stream->Write(info.name, 64);
    
    // Duration, ticks per second, num channels
    m_stream->WriteFloat(info.duration);
    m_stream->WriteFloat(info.ticksPerSecond);
    m_stream->WriteUInt(info.numChannels);
    
    EndChunk(startPos);
}

void AnimWriter::WriteChannelChunk(const AnimationChannel& channel)
{
    long startPos;
    BeginChunk(ANIM_CHUNK_CHAN, &startPos);
    
    // Bone name
    m_stream->WriteCString(channel.boneName);
    
    // Number of keyframes
    u32 numKeys = channel.keyframes.size();
    m_stream->WriteUInt(numKeys);
    
    // Keyframes
    for (const auto& key : channel.keyframes)
    {
        // Time
        m_stream->WriteFloat(key.time);
        
        // Position
        m_stream->WriteFloat(key.position.x);
        m_stream->WriteFloat(key.position.y);
        m_stream->WriteFloat(key.position.z);
        
        // Rotation (quaternion)
        m_stream->WriteFloat(key.rotation.x);
        m_stream->WriteFloat(key.rotation.y);
        m_stream->WriteFloat(key.rotation.z);
        m_stream->WriteFloat(key.rotation.w);
        
        // Scale
        m_stream->WriteFloat(key.scale.x);
        m_stream->WriteFloat(key.scale.y);
        m_stream->WriteFloat(key.scale.z);
    }
    
    EndChunk(startPos);
}

bool AnimWriter::SaveAll(const std::string& baseFilename, const std::vector<SimpleAnimation>& animations)
{
    if (animations.empty())
    {
        std::cout << "[AnimWriter] No animations to export" << std::endl;
        return true;
    }
    
    if (animations.size() == 1)
    {
        // Single animation - usa o nome base
        return Save(baseFilename, animations[0]);
    }
    
    // Multiple animations - cria ficheiros separados
    for (const auto& anim : animations)
    {
        std::string animFile = baseFilename;
        
        // Insere nome da animation antes da extensão
        size_t dot = animFile.find_last_of('.');
        if (dot != std::string::npos)
        {
            animFile = animFile.substr(0, dot) + "_" + anim.name + animFile.substr(dot);
        }
        else
        {
            animFile = animFile + "_" + anim.name + ".anim";
        }
        
        if (!Save(animFile, anim))
        {
            return false;
        }
    }
    
    std::cout << "[AnimWriter] Exported " << animations.size() << " animations" << std::endl;
    return true;
}