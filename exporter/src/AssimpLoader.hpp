#pragma once
#include "SimpleMesh.hpp"
#include <string>
#include <vector>
#include <map>

// Forward declarations (evita incluir Assimp no header)
struct aiScene;
struct aiNode;
struct aiMesh;
struct aiMaterial;
struct aiBone;

 
struct ConversionOptions
{
    bool optimize = true;
    bool generateUVs = false;
    bool generateNormals = false;      // Gera normais se faltarem
    bool generateSmoothNormals = true; // Smooth vs flat normals
    bool generateTangents = true;
    bool mergeMeshes = false;
    bool verbose = false;
    int optimizationLevel = 1;
};

class AssimpLoader
{
public:
    AssimpLoader();
    ~AssimpLoader();
    
    bool Load(const std::string& filename, SimpleMesh* mesh, const ConversionOptions& opts);
    
    void SetVerbose(bool verbose) { m_verbose = verbose; }
    
    bool HasAnimations() const { return !m_animations.empty(); }
    const std::vector<SimpleAnimation>& GetAnimations() const { return m_animations; }
    
private:
    bool m_verbose;
    std::map<std::string, u32> m_boneMap;
    std::vector<SimpleAnimation> m_animations;  
    
    u32 BuildImportFlags(const ConversionOptions& opts);
    void ProcessNode(aiNode* node, const aiScene* scene, SimpleMesh* mesh);
    void ProcessMesh(aiMesh* assimpMesh, const aiScene* scene, SimpleMesh* mesh);
    void ProcessMaterials(const aiScene* scene, SimpleMesh* mesh);
    void ProcessBones(const aiScene* scene, SimpleMesh* mesh);
    void ProcessAnimations(const aiScene* scene);  
    void ProcessSkinning(aiMesh* assimpMesh, SimpleMeshBuffer* buffer, SimpleMesh* mesh);
    u32 FindOrCreateBone(const std::string& boneName, SimpleMesh* mesh);
    void BuildBoneHierarchy(aiNode* node, const aiScene* scene, SimpleMesh* mesh);
    void BuildNodeMap(aiNode* node, std::map<std::string, aiNode*>& nodeMap);
    void ValidateBoneHierarchy(SimpleMesh* mesh);
    void PrintBoneTree(SimpleMesh* mesh, u32 boneIndex, int depth);
};