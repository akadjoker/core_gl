#pragma once
#include "SimpleMesh.hpp"
#include <string>

// Native reader for Ogre's binary .mesh format (MeshSerializer_v1.100,
// current since Ogre 1.10), static meshes only — no skeleton/bone weights/
// animation. Verified byte-for-byte against the real Ogre 14.5.1 source
// (OgreMain/src/{OgreSerializer,OgreMeshSerializerImpl}.cpp) and cross-
// checked against real files (Columns.mesh, RomanBathUpper/Lower.mesh).
//
// Chunk framing: [u16 id][u32 length] (6 bytes, little-endian), length
// includes the 6-byte header. M_HEADER is the one exception: just a u16 id
// followed by the version string, no length field. Strings are '\n'-
// terminated (Serializer::readString == DataStream::getLine), not length-
// prefixed.
class OgreMeshLoader
{
public:
    // Populates `mesh` with one SimpleMeshBuffer per Ogre submesh and one
    // SimpleMaterial per distinct material name (name only — diffuse/
    // specular/textures need the separate .material script, not parsed
    // here). Returns false and prints an error on any format mismatch —
    // never silently produces wrong geometry.
    bool Load(const std::string& filename, SimpleMesh* mesh);

    void SetVerbose(bool verbose) { m_verbose = verbose; }

private:
    bool m_verbose = false;
};
