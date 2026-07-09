#pragma once

// Procedural meshes shared by the demos.

#include <scene/Mesh.hpp>

// unit cube, base on y=0 (y in [0,1]), 24 verts with face normals
inline void demoBuildCube(Mesh& mesh)
{
    // clang-format off
    const float P[6][4][3] = {
        {{-.5f,0,-.5f},{-.5f,1,-.5f},{ .5f,1,-.5f},{ .5f,0,-.5f}}, // -Z
        {{-.5f,0, .5f},{ .5f,0, .5f},{ .5f,1, .5f},{-.5f,1, .5f}}, // +Z
        {{-.5f,0,-.5f},{-.5f,0, .5f},{-.5f,1, .5f},{-.5f,1,-.5f}}, // -X
        {{ .5f,0,-.5f},{ .5f,1,-.5f},{ .5f,1, .5f},{ .5f,0, .5f}}, // +X
        {{-.5f,0,-.5f},{ .5f,0,-.5f},{ .5f,0, .5f},{-.5f,0, .5f}}, // -Y
        {{-.5f,1,-.5f},{-.5f,1, .5f},{ .5f,1, .5f},{ .5f,1,-.5f}}, // +Y
    };
    const float N[6][3] = {{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}};
    // clang-format on
    MeshVertex verts[24];
    u16 idx[36];
    for (int f = 0; f < 6; ++f)
    {
        for (int v = 0; v < 4; ++v)
        {
            MeshVertex& mv = verts[f * 4 + v];
            mv.position = Vec3(P[f][v][0], P[f][v][1], P[f][v][2]);
            mv.normal = Vec3(N[f][0], N[f][1], N[f][2]);
            mv.tangent = Vec4(1.f, 0.f, 0.f, 1.f);
            mv.uv = Vec2((float)(v == 1 || v == 2), (float)(v >= 2));
        }
        u16 base = (u16)(f * 4);
        u16* o = &idx[f * 6];
        o[0] = base;
        o[1] = base + 1;
        o[2] = base + 2;
        o[3] = base;
        o[4] = base + 2;
        o[5] = base + 3;
    }
    mesh.set_data(verts, 24, idx, 36);
    mesh.upload();
}

// flat XZ plane at y=0, normal +Y, uv tiled `uvRepeat` times
inline void demoBuildPlane(Mesh& mesh, float half, float uvRepeat = 1.f)
{
    MeshVertex verts[4];
    const float pos[4][3] = {
        {-half, 0, -half}, {half, 0, -half}, {half, 0, half}, {-half, 0, half}};
    for (int i = 0; i < 4; ++i)
    {
        verts[i].position = Vec3(pos[i][0], pos[i][1], pos[i][2]);
        verts[i].normal = Vec3(0.f, 1.f, 0.f);
        verts[i].tangent = Vec4(1.f, 0.f, 0.f, 1.f);
        verts[i].uv = Vec2((float)(i == 1 || i == 2) * uvRepeat, (float)(i >= 2) * uvRepeat);
    }
    const u16 idx[6] = {0, 2, 1, 0, 3, 2}; // CCW seen from above
    mesh.set_data(verts, 4, idx, 6);
    mesh.upload();
}
