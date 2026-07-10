#pragma once
#include "Types.hpp"

// Mesh Converter Tool v1.0.0
// Copyright (c) 2025 Luis Santos (aka djoker)

constexpr u32 MESH_MAGIC = 0x4D455348; // "MESH"
constexpr u32 MESH_VERSION = 100;      // v1.00

// Flags

constexpr u32 BUFFER_FLAG_SKINNED = 1 << 0;   // Tem skinning data
constexpr u32 BUFFER_FLAG_TANGENTS = 1 << 1;  // Tem tangents 
constexpr u32 BUFFER_FLAG_COLORS = 1 << 2;    // Tem vertex colors 

// Chunks
constexpr u32 CHUNK_HEAD = 0x48454144; // "HEAD"
constexpr u32 CHUNK_MATS = 0x4D415453; // "MATS"
constexpr u32 CHUNK_BUFF = 0x42554646; // "BUFF"
constexpr u32 CHUNK_VRTS = 0x56525453; // "VRTS"
constexpr u32 CHUNK_IDXS = 0x49445853; // "IDXS"
constexpr u32 CHUNK_SKIN = 0x534B494E; // "SKIN"
constexpr u32 CHUNK_SKEL = 0x534B454C; // "SKEL"
constexpr u32 CHUNK_TANG = 0x54414E47; // "TANG" - Tangents/Bitangents

// Animation format
constexpr u32 ANIM_MAGIC = 0x414E494D;   // "ANIM"
constexpr u32 ANIM_VERSION = 100;        // v1.00

// Chunk IDs
constexpr u32 ANIM_CHUNK_INFO = 0x494E464F;  // "INFO" - Animation info
constexpr u32 ANIM_CHUNK_CHAN = 0x4348414E;  // "CHAN" - Channel (per bone)
constexpr u32 ANIM_CHUNK_KEYS = 0x4B455953;  // "KEYS" - Keyframes
