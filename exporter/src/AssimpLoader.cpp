#include "Types.hpp"
#include "AssimpLoader.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>

AssimpLoader::AssimpLoader()
    : m_verbose(false)
{
}

AssimpLoader::~AssimpLoader()
{
}
u32 AssimpLoader::BuildImportFlags(const ConversionOptions &opts)
{

 
    u32 flags = 
        aiProcess_Triangulate              |
        aiProcess_JoinIdenticalVertices    |
        aiProcess_ImproveCacheLocality     |
        aiProcess_OptimizeMeshes           |
        aiProcess_OptimizeGraph            |
        aiProcess_RemoveRedundantMaterials |
        aiProcess_FindDegenerates          |
        aiProcess_FindInvalidData          |
        aiProcess_ValidateDataStructure    |
        aiProcess_SortByPType              |
        aiProcess_LimitBoneWeights         |
        aiProcess_FlipUVs                  |
        aiProcess_MakeLeftHanded;
    
   
    if (opts.generateNormals || opts.generateSmoothNormals)
    {
        if (opts.generateSmoothNormals)
            flags |= aiProcess_GenSmoothNormals;
        else
            flags |= aiProcess_GenNormals;
    }
    
 
    if (opts.generateUVs)
    {
        flags |= aiProcess_GenUVCoords;
        flags |= aiProcess_TransformUVCoords;
    }
    
    // Tangents (precisa de normais e UVs existentes)
    if (opts.generateTangents)
    {
        flags |= aiProcess_CalcTangentSpace;
    }
    
    // Level 2
    if (opts.optimizationLevel >= 2)
    {
        flags |= aiProcess_SplitLargeMeshes;
        flags |= aiProcess_SplitByBoneCount;
    }
    
    return flags;
}


bool AssimpLoader::Load(const std::string &filename, SimpleMesh *mesh, const ConversionOptions &opts)
{
    if (!mesh)
    {
        std::cerr << "[AssimpLoader] Invalid mesh pointer" << std::endl;
        return false;
    }

    Assimp::Importer importer;

    // Build flags
        u32 flags = BuildImportFlags(opts);


      flags = aiProcess_JoinIdenticalVertices |
                aiProcess_ValidateDataStructure |
                aiProcess_ImproveCacheLocality |
                aiProcess_RemoveRedundantMaterials |
                aiProcess_FindDegenerates |
                aiProcess_FindInvalidData |
              //  aiProcess_GenUVCoords |
             //   aiProcess_TransformUVCoords |
                aiProcess_LimitBoneWeights |
                aiProcess_OptimizeMeshes |
                aiProcess_SplitByBoneCount |
                //aiProcess_GenSmoothNormals |
                aiProcess_SplitLargeMeshes |
                aiProcess_Triangulate |
               // aiProcess_MakeLeftHanded |
               // aiProcess_FlipUVs |
                aiProcess_SortByPType |
             //   aiProcess_OptimizeGraph |
                //aiProcess_GenNormals |
                0;



		//if ( withChildren == 0 ) ppsteps |= aiProcess_PreTransformVertices; // remove scene graph and pre-transforms all vertices with the local transformation matrices of their nodes.
	//	else ppsteps |= aiProcess_OptimizeGraph;


    if (m_verbose)
    {
        std::cout << "Import flags: 0x" << std::hex << flags << std::dec << std::endl;
    }

    importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_NORMALS);  
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT );





    // Load scene
    const aiScene *scene = importer.ReadFile(filename, flags);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cerr << "[AssimpLoader] Error: " << importer.GetErrorString() << std::endl;
        return false;
    }

    std::cout << "Loaded: " << filename << std::endl;
    std::cout << "  Meshes: " << scene->mNumMeshes << std::endl;
    std::cout << "  Materials: " << scene->mNumMaterials << std::endl;

    // Conta vértices/faces totais
    u32 totalVerts = 0, totalFaces = 0;
    for (u32 i = 0; i < scene->mNumMeshes; i++)
    {
        totalVerts += scene->mMeshes[i]->mNumVertices;
        totalFaces += scene->mMeshes[i]->mNumFaces;
    }
    std::cout << "  Total vertices: " << totalVerts << std::endl;
    std::cout << "  Total faces: " << totalFaces << std::endl;

    // Clear bone map
    m_boneMap.clear();

    if (scene->mNumAnimations > 0)
    {
        std::cout << "  Animations: " << scene->mNumAnimations << std::endl;
        ProcessAnimations(scene);
    }

    // Process data
    ProcessMaterials(scene, mesh);
    ProcessBones(scene, mesh);
    ProcessNode(scene->mRootNode, scene, mesh);

    std::cout << "Conversion complete:" << std::endl;
    std::cout << "  Output buffers: " << mesh->GetBufferCount() << std::endl;
    std::cout << "  Output materials: " << mesh->GetMaterialCount() << std::endl;

    if (mesh->HasSkeleton())
        std::cout << "  Output bones: " << mesh->bones.size() << std::endl;

    return true;
}

void AssimpLoader::ProcessAnimations(const aiScene *scene)
{
    for (u32 i = 0; i < scene->mNumAnimations; i++)
    {
        aiAnimation *anim = scene->mAnimations[i];

        SimpleAnimation simpleAnim;
        simpleAnim.name = anim->mName.C_Str();
        if (simpleAnim.name.empty())
            simpleAnim.name = "anim_" + std::to_string(i);

        simpleAnim.duration = static_cast<float>(anim->mDuration);
        simpleAnim.ticksPerSecond = anim->mTicksPerSecond > 0 ? static_cast<float>(anim->mTicksPerSecond) : 25.0f;

        if (m_verbose)
        {
            std::cout << "  Animation: " << simpleAnim.name
                      << " (" << simpleAnim.duration << " ticks, "
                      << anim->mNumChannels << " channels)" << std::endl;
        }

        // Process channels (um por bone)
        for (u32 j = 0; j < anim->mNumChannels; j++)
        {
            aiNodeAnim *channel = anim->mChannels[j];

            AnimationChannel simpleChannel;
            simpleChannel.boneName = channel->mNodeName.C_Str();

            // Determina número de keyframes (max entre pos/rot/scale)
            u32 numKeys = std::max({channel->mNumPositionKeys,
                                    channel->mNumRotationKeys,
                                    channel->mNumScalingKeys});

            for (u32 k = 0; k < numKeys; k++)
            {
                Keyframe key;
                key.time = 0.0f;

                // Position
                if (k < channel->mNumPositionKeys)
                {
                    aiVectorKey posKey = channel->mPositionKeys[k];
                    key.time = static_cast<float>(posKey.mTime);
                    key.position = Vec3(posKey.mValue.x, posKey.mValue.y, posKey.mValue.z);
                }
                else if (channel->mNumPositionKeys > 0)
                {
                    // Usa último conhecido
                    aiVectorKey posKey = channel->mPositionKeys[channel->mNumPositionKeys - 1];
                    key.position = Vec3(posKey.mValue.x, posKey.mValue.y, posKey.mValue.z);
                }

                // Rotation
                if (k < channel->mNumRotationKeys)
                {
                    aiQuatKey rotKey = channel->mRotationKeys[k];
                    key.time = std::max(key.time, static_cast<float>(rotKey.mTime));
                    key.rotation = Quat(rotKey.mValue.x, rotKey.mValue.y,
                                        rotKey.mValue.z, rotKey.mValue.w);
                }
                else if (channel->mNumRotationKeys > 0)
                {
                    aiQuatKey rotKey = channel->mRotationKeys[channel->mNumRotationKeys - 1];
                    key.rotation = Quat(rotKey.mValue.x, rotKey.mValue.y,
                                        rotKey.mValue.z, rotKey.mValue.w);
                }

                // Scale
                if (k < channel->mNumScalingKeys)
                {
                    aiVectorKey scaleKey = channel->mScalingKeys[k];
                    key.time = std::max(key.time, static_cast<float>(scaleKey.mTime));
                    key.scale = Vec3(scaleKey.mValue.x, scaleKey.mValue.y, scaleKey.mValue.z);
                }
                else
                {
                    key.scale = Vec3(1.0f, 1.0f, 1.0f);
                }

                simpleChannel.keyframes.push_back(key);
            }

            if (!simpleChannel.keyframes.empty())
            {
                simpleAnim.channels.push_back(simpleChannel);
            }
        }

        if (!simpleAnim.channels.empty())
        {
            m_animations.push_back(simpleAnim);
        }
    }
}

void AssimpLoader::ProcessMaterials(const aiScene *scene, SimpleMesh *mesh)
{
    for (u32 i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial *mat = scene->mMaterials[i];

        aiString name;
        mat->Get(AI_MATKEY_NAME, name);

        SimpleMaterial *simpleMat = mesh->AddMaterial(name.C_Str());

        // Colors
        aiColor3D diffuse(0.8f, 0.8f, 0.8f);
        mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        simpleMat->diffuse = Vec3(diffuse.r, diffuse.g, diffuse.b);

        aiColor3D specular(0.5f, 0.5f, 0.5f);
        mat->Get(AI_MATKEY_COLOR_SPECULAR, specular);
        simpleMat->specular = Vec3(specular.r, specular.g, specular.b);

        float shininess = 32.0f;
        mat->Get(AI_MATKEY_SHININESS, shininess);
        simpleMat->shininess = shininess;

        simpleMat->textureCount = 0;

        // Diffuse texture (layer 0)
        if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0)
        {
            aiString path;
            if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
            {
                std::string texPath = path.C_Str();

                if (!texPath.empty() && texPath.length() < 256)
                {

                    bool isValid = true;
                    for (char c : texPath)
                    {
                        if (c != 0 && (c < 32 || c > 126))
                        {
                            isValid = false;
                            break;
                        }
                    }

                    if (isValid)
                    {
                        simpleMat->textures[simpleMat->textureCount] = texPath;
                        simpleMat->textureCount++;
                    }
                }
            }
        }

        if (m_verbose)
        {
            std::cout << "  Material [" << i << "]: " << simpleMat->name
                      << " (" << (int)simpleMat->textureCount << " textures)" << std::endl;
            for (u8 j = 0; j < simpleMat->textureCount; j++)
            {
                std::cout << "    [" << (int)j << "] " << simpleMat->textures[j] << std::endl;
            }
        }
    }
}

// void AssimpLoader::ProcessMaterials(const aiScene* scene, SimpleMesh* mesh)
// {
//     for (u32 i = 0; i < scene->mNumMaterials; i++)
//     {
//         aiMaterial* mat = scene->mMaterials[i];

//         aiString name;
//         mat->Get(AI_MATKEY_NAME, name);

//         SimpleMaterial* simpleMat = mesh->AddMaterial(name.C_Str());

//         // Diffuse color
//         aiColor3D diffuse(0.8f, 0.8f, 0.8f);
//         mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
//         simpleMat->diffuse = Vec3(diffuse.r, diffuse.g, diffuse.b);

//         // Specular color
//         aiColor3D specular(0.5f, 0.5f, 0.5f);
//         mat->Get(AI_MATKEY_COLOR_SPECULAR, specular);
//         simpleMat->specular = Vec3(specular.r, specular.g, specular.b);

//         // Shininess
//         float shininess = 32.0f;
//         mat->Get(AI_MATKEY_SHININESS, shininess);
//         simpleMat->shininess = shininess;

//      simpleMat->textureCount = 0;

//         // Layer 0: Diffuse/Albedo
//         if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0)
//         {
//             aiString path;
//             if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }

//         // Layer 1: Normal map
//         if (mat->GetTextureCount(aiTextureType_NORMALS) > 0)
//         {
//             aiString path;
//             if (mat->GetTexture(aiTextureType_NORMALS, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }
//         else if (mat->GetTextureCount(aiTextureType_HEIGHT) > 0)
//         {
//             // Fallback: bump map
//             aiString path;
//             if (mat->GetTexture(aiTextureType_HEIGHT, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }

//         // Layer 2: Specular map
//         if (mat->GetTextureCount(aiTextureType_SPECULAR) > 0)
//         {
//             aiString path;
//             if (mat->GetTexture(aiTextureType_SPECULAR, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }

//         // Layer 3: Emissive/Lightmap
//         if (mat->GetTextureCount(aiTextureType_EMISSIVE) > 0)
//         {
//             aiString path;
//             if (mat->GetTexture(aiTextureType_EMISSIVE, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }
//         else if (mat->GetTextureCount(aiTextureType_LIGHTMAP) > 0)
//         {
//             aiString path;
//             if (mat->GetTexture(aiTextureType_LIGHTMAP, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }

//         // Layer 4: Ambient Occlusion
//         if (mat->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION) > 0)
//         {
//             aiString path;
//             if (mat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }

//         // Layer 5: Metalness (PBR)
//         if (mat->GetTextureCount(aiTextureType_METALNESS) > 0)
//         {
//             aiString path;
//             if (mat->GetTexture(aiTextureType_METALNESS, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }

//         // Layer 6: Roughness (PBR)
//         if (mat->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0)
//         {
//             aiString path;
//             if (mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }

//         // Layer 7: Opacity/Alpha
//         if (mat->GetTextureCount(aiTextureType_OPACITY) > 0)
//         {
//             aiString path;
//             if (mat->GetTexture(aiTextureType_OPACITY, 0, &path) == AI_SUCCESS)
//             {
//                 simpleMat->textures[simpleMat->textureCount] = path.C_Str();
//                 simpleMat->textureCount++;
//             }
//         }

//         if (m_verbose)
//         {
//             std::cout << "  Material [" << i << "]: " << simpleMat->name
//                       << " (" << (int)simpleMat->textureCount << " textures)" << std::endl;
//             for (u8 j = 0; j < simpleMat->textureCount; j++)
//             {
//                 std::cout << "    [" << (int)j << "] " << simpleMat->textures[j] << std::endl;
//             }
//         }
//     }
// }


void AssimpLoader::BuildBoneHierarchy(aiNode* node, const aiScene* scene, SimpleMesh* mesh)
{
    std::string nodeName = node->mName.C_Str();
    
    // Se este node é um bone
    auto it = m_boneMap.find(nodeName);
    if (it != m_boneMap.end())
    {
        u32 boneIndex = it->second;
        Bone &bone = mesh->bones[boneIndex];
        
        // Pega local transform do node
        aiMatrix4x4 localTrans = node->mTransformation;
        for (int j = 0; j < 16; j++)
            bone.localTransform.m[j] = localTrans[j / 4][j % 4];
        
        // Procura parent
        if (node->mParent)
        {
            std::string parentName = node->mParent->mName.C_Str();
            auto parentIt = m_boneMap.find(parentName);
            if (parentIt != m_boneMap.end())
            {
                bone.parentIndex = parentIt->second;
                
                if (m_verbose)
                    std::cout << "    " << bone.name << " → parent: " 
                              << mesh->bones[bone.parentIndex].name << std::endl;
            }
        }
    }
    
    // Recursivo para filhos
    for (u32 i = 0; i < node->mNumChildren; i++)
    {
        BuildBoneHierarchy(node->mChildren[i], scene, mesh);
    }
}

void CopyMatrix(const aiMatrix4x4& src, Mat4& dst)
{
    //  (mantém column-major)
    // dst.m[0]  = src.a1; dst.m[1]  = src.a2; dst.m[2]  = src.a3; dst.m[3]  = src.a4;
    // dst.m[4]  = src.b1; dst.m[5]  = src.b2; dst.m[6]  = src.b3; dst.m[7]  = src.b4;
    // dst.m[8]  = src.c1; dst.m[9]  = src.c2; dst.m[10] = src.c3; dst.m[11] = src.c4;
    // dst.m[12] = src.d1; dst.m[13] = src.d2; dst.m[14] = src.d3; dst.m[15] = src.d4;
    dst.m[0]  = src.a1; dst.m[1]  = src.b1; dst.m[2]  = src.c1; dst.m[3]  = src.d1;
    dst.m[4]  = src.a2; dst.m[5]  = src.b2; dst.m[6]  = src.c2; dst.m[7]  = src.d2;
    dst.m[8]  = src.a3; dst.m[9]  = src.b3; dst.m[10] = src.c3; dst.m[11] = src.d3;
    dst.m[12] = src.a4; dst.m[13] = src.b4; dst.m[14] = src.c4; dst.m[15] = src.d4;

 

}



 


void AssimpLoader::ProcessBones(const aiScene* scene, SimpleMesh* mesh)
{
    if (m_verbose)
        std::cout << "\n=== Processing Skeleton ===" << std::endl;
    
    // 1. Coleta todos os bones
    std::map<std::string, aiBone*> allBones;
    
    for (u32 m = 0; m < scene->mNumMeshes; m++)
    {
        aiMesh* assimpMesh = scene->mMeshes[m];
        if (!assimpMesh->HasBones()) continue;
        
        for (u32 i = 0; i < assimpMesh->mNumBones; i++)
        {
            aiBone* aiBone = assimpMesh->mBones[i];
            std::string boneName = aiBone->mName.C_Str();
            
            if (allBones.find(boneName) == allBones.end())
            {
                allBones[boneName] = aiBone;
            }
        }
    }
    
    if (allBones.empty())
        return;
    
    if (m_verbose)
        std::cout << "  Found " << allBones.size() << " unique bones" << std::endl;
    
    // 2. Cria bones e mapeia nodes
    std::map<std::string, aiNode*> nodeMap;
    BuildNodeMap(scene->mRootNode, nodeMap);
    
    for (const auto& pair : allBones)
    {
        const std::string& boneName = pair.first;
        aiBone* aiBone = pair.second;
        
        Bone bone;
        bone.name = boneName;
        bone.parentIndex = -1;
        
  
      
 

        CopyMatrix(aiBone->mOffsetMatrix, bone.inverseBindPose);

     
        
        // Pega local transform do node (se existir)
        auto nodeIt = nodeMap.find(boneName);
        if (nodeIt != nodeMap.end())
        {
            CopyMatrix(nodeIt->second->mTransformation, bone.localTransform);
        }
        else
        {
            // Identity se node não existir
            for (int j = 0; j < 16; j++)
                bone.localTransform.m[j] = (j % 5 == 0) ? 1.0f : 0.0f;
        }
        
        u32 boneIndex = mesh->bones.size();
        mesh->bones.push_back(bone);
        m_boneMap[boneName] = boneIndex;
    }
    
    // 3. CRÍTICO: Preenche hierarquia usando o scene graph
    for (const auto& pair : nodeMap)
    {
        const std::string& nodeName = pair.first;
        aiNode* node = pair.second;
        
        // Se este node é um bone
        auto boneIt = m_boneMap.find(nodeName);
        if (boneIt != m_boneMap.end())
        {
            u32 boneIndex = boneIt->second;
            Bone& bone = mesh->bones[boneIndex];
            
            // Procura parent no scene graph
            if (node->mParent)
            {
                aiNode* parentNode = node->mParent;
                std::string parentName = parentNode->mName.C_Str();
                
                // LOOP: Sobe na hierarquia até encontrar um bone
                while (parentNode)
                {
                    parentName = parentNode->mName.C_Str();
                    auto parentIt = m_boneMap.find(parentName);
                    
                    if (parentIt != m_boneMap.end())
                    {
                        // Encontrou parent bone!
                        bone.parentIndex = parentIt->second;
                        
                        if (m_verbose)
                            std::cout << "    " << bone.name << " → " 
                                      << mesh->bones[bone.parentIndex].name << std::endl;
                        break;
                    }
                    
                    // Sobe mais um nível
                    parentNode = parentNode->mParent;
                }
            }
        }
    }
    
    // 4. Valida hierarquia
    ValidateBoneHierarchy(mesh);
}

void AssimpLoader::BuildNodeMap(aiNode* node, std::map<std::string, aiNode*>& nodeMap)
{
    std::string nodeName = node->mName.C_Str();
    nodeMap[nodeName] = node;
    
    // Recursivo
    for (u32 i = 0; i < node->mNumChildren; i++)
    {
        BuildNodeMap(node->mChildren[i], nodeMap);
    }
}

void AssimpLoader::ValidateBoneHierarchy(SimpleMesh* mesh)
{
    if (!m_verbose) return;
    
    std::cout << "\n  Bone Hierarchy:" << std::endl;
    
    // Conta roots
    int rootCount = 0;
    for (size_t i = 0; i < mesh->bones.size(); i++)
    {
        const Bone& bone = mesh->bones[i];
        if (bone.parentIndex < 0)
        {
            PrintBoneTree(mesh, i, 0);
            rootCount++;
        }
    }
    
    if (rootCount == 1)
    {
        std::cout << "  ✓ Single root bone (OK)" << std::endl;
    }
    else if (rootCount > 1)
    {
        std::cout << "  ⚠ Warning: " << rootCount << " root bones detected!" << std::endl;
    }
}


void AssimpLoader::PrintBoneTree(SimpleMesh* mesh, u32 boneIndex, int depth)
{
    const Bone& bone = mesh->bones[boneIndex];
    
    std::string indent(depth * 2, ' ');
    std::cout << "  " << indent << "[" << boneIndex << "] " << bone.name << std::endl;
    
    // Print children
    for (size_t i = 0; i < mesh->bones.size(); i++)
    {
        if (mesh->bones[i].parentIndex == (s32)boneIndex)
        {
            PrintBoneTree(mesh, i, depth + 1);
        }
    }
}

void AssimpLoader::ProcessNode(aiNode *node, const aiScene *scene, SimpleMesh *mesh)
{
    // Process all meshes in this node
    for (u32 i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh *assimpMesh = scene->mMeshes[node->mMeshes[i]];
        ProcessMesh(assimpMesh, scene, mesh);
    }

    // Recursively process children
    for (u32 i = 0; i < node->mNumChildren; i++)
    {
        ProcessNode(node->mChildren[i], scene, mesh);
    }
}

void AssimpLoader::ProcessMesh(aiMesh* assimpMesh, const aiScene* scene, SimpleMesh* mesh)
{
    u32 materialIndex = assimpMesh->mMaterialIndex;
    SimpleMeshBuffer* buffer = mesh->AddBuffer(materialIndex);
    
    // Vertices
    for (u32 i = 0; i < assimpMesh->mNumVertices; i++)
    {
        aiVector3D pos = assimpMesh->mVertices[i];
        aiVector3D normal = assimpMesh->HasNormals() ? 
            assimpMesh->mNormals[i] : aiVector3D(0, 1, 0);
        aiVector3D texCoord = assimpMesh->HasTextureCoords(0) ? 
            assimpMesh->mTextureCoords[0][i] : aiVector3D(0, 0, 0);
        
        buffer->AddVertex(pos.x, pos.y, pos.z,
                         normal.x, normal.y, normal.z,
                         texCoord.x, texCoord.y);
    }
    
    // Faces
    for (u32 i = 0; i < assimpMesh->mNumFaces; i++)
    {
        aiFace face = assimpMesh->mFaces[i];
        if (face.mNumIndices == 3)
        {
            buffer->AddFace(face.mIndices[0], face.mIndices[1], face.mIndices[2]);
        }
    }
    
    // CRÍTICO: Processar skinning!
    if (assimpMesh->HasBones())
    {
        ProcessSkinning(assimpMesh, buffer, mesh);
    }
    
    if (m_verbose)
    {
        std::cout << "  Mesh: " << assimpMesh->mName.C_Str() 
                  << " (" << assimpMesh->mNumVertices << " verts, "
                  << assimpMesh->mNumFaces << " faces";
        
        if (assimpMesh->HasBones())
            std::cout << ", skinned with " << assimpMesh->mNumBones << " bones";
        
        std::cout << ")" << std::endl;
    }
}

void AssimpLoader::ProcessSkinning(aiMesh* assimpMesh, SimpleMeshBuffer* buffer, SimpleMesh* mesh)
{
    u32 numVertices = assimpMesh->mNumVertices;
    
    // Inicializa skinning data (zeros)
    std::vector<VertexSkin> skinData(numVertices);
    for (auto& skin : skinData)
    {
        for (int i = 0; i < 4; i++)
        {
            skin.boneIDs[i] = 0;
            skin.weights[i] = 0.0f;
        }
    }
    
    // Para cada bone nesta mesh
    for (u32 b = 0; b < assimpMesh->mNumBones; b++)
    {
        aiBone* aiBone = assimpMesh->mBones[b];
        std::string boneName = aiBone->mName.C_Str();
        
        // Encontra índice do bone na SimpleMesh
        auto it = m_boneMap.find(boneName);
        if (it == m_boneMap.end())
        {
            std::cerr << "Warning: Bone '" << boneName << "' not found in bone map!" << std::endl;
            continue;
        }
        
        u32 boneIndex = it->second;
        
        // Para cada vértice influenciado por este bone
        for (u32 w = 0; w < aiBone->mNumWeights; w++)
        {
            aiVertexWeight weight = aiBone->mWeights[w];
            u32 vertexID = weight.mVertexId;
            float weightValue = weight.mWeight;
            
            if (vertexID >= numVertices)
                continue;
            
            // Adiciona weight ao vértice (até 4 bones por vértice)
            VertexSkin& skin = skinData[vertexID];
            
            // Encontra slot vazio ou menor peso
            for (int i = 0; i < 4; i++)
            {
                if (skin.weights[i] == 0.0f || weightValue > skin.weights[i])
                {
                    // Shift weights para a direita
                    for (int j = 3; j > i; j--)
                    {
                        skin.boneIDs[j] = skin.boneIDs[j-1];
                        skin.weights[j] = skin.weights[j-1];
                    }
                    
                    // Insere novo weight
                    skin.boneIDs[i] = boneIndex;
                    skin.weights[i] = weightValue;
                    break;
                }
            }
        }
    }
    
    // Normaliza weights (soma = 1.0)
    for (u32 i = 0; i < numVertices; i++)
    {
        VertexSkin& skin = skinData[i];
        
        float sum = skin.weights[0] + skin.weights[1] + skin.weights[2] + skin.weights[3];
        
        if (sum > 0.0f)
        {
            skin.weights[0] /= sum;
            skin.weights[1] /= sum;
            skin.weights[2] /= sum;
            skin.weights[3] /= sum;
        }
    }
    
    // Marca buffer como skinned
    buffer->SetSkinData(skinData);
    
    if (m_verbose)
    {
        std::cout << "    Skinning: " << numVertices << " vertices, "
                  << assimpMesh->mNumBones << " bones" << std::endl;
    }
}

u32 AssimpLoader::FindOrCreateBone(const std::string &boneName, SimpleMesh *mesh)
{
    auto it = m_boneMap.find(boneName);
    if (it != m_boneMap.end())
        return it->second;

    // Create new bone
    Bone bone;
    bone.name = boneName;
    bone.parentIndex = -1;

    u32 index = mesh->bones.size();
    mesh->bones.push_back(bone);
    m_boneMap[boneName] = index;

    return index;
}
