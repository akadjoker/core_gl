#pragma once
#include "MeshFormat.hpp"
#include "SimpleMesh.hpp"
#include <string>

class Stream;

class MeshWriter
{
public:
    bool Save(  SimpleMesh* mesh, const std::string& filename);
    
    MeshWriter();
    ~MeshWriter();
private:
    Stream* m_stream;
    
    void BeginChunk(u32 chunkId, long* posOut);
    void EndChunk(long startPos);
    void WriteCString(const std::string& str);
    
    void WriteMaterialsChunk(  SimpleMesh* mesh);
    void WriteSkeletonChunk(  SimpleMesh* mesh);
    void WriteBufferChunk(  SimpleMeshBuffer* buffer);
    void WriteVerticesChunk(  SimpleMeshBuffer* buffer);
    void WriteIndicesChunk(  SimpleMeshBuffer* buffer);
    void WriteSkinChunk(  SimpleMeshBuffer* buffer);
};


class AnimWriter
{
public:
    AnimWriter();
    ~AnimWriter();
    
    bool Save(const std::string& filename, const SimpleAnimation& animation);
    bool SaveAll(const std::string& baseFilename, const std::vector<SimpleAnimation>& animations);
    
private:
    Stream* m_stream;
    
    void BeginChunk(u32 chunkId, long* posOut);
    void EndChunk(long startPos);
    
    void WriteInfoChunk(const AnimationInfo& info);
    void WriteChannelChunk(const AnimationChannel& channel);
};