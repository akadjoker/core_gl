#pragma once

// AI ship: its own HoverJet (see HoverPhysics.hpp) plus a mesh/exhaust and
// enough steering to chase a track's gates forever. Pulled out of main.cpp
// so the setup/update logic isn't tangled up with the player/camera/editor
// code living there.

#include "HoverPhysics.hpp"
#include <scene/AssetManager.hpp>
#include <scene/Material.hpp>
#include <scene/Mesh.hpp>
#include <scene/MeshInstance.hpp>
#include <scene/ParticleSystemNode.hpp>
#include <scene/Scene.hpp>
#include <scene/TerrainNode.hpp>
#include <vector>

struct Npc
{
    HoverJet jet;
    MeshInstance* mesh = nullptr;
    int targetGate = 0;

    // `startGate`/`aimGate` place and orient it like a player spawn (see
    // main.cpp's own jet.init() + facing block) — same pattern, just as a
    // reusable function since there are several of these now.
    static Npc spawn(Scene& scene, assets::AssetManager& assets, const char* name,
                     const char* meshFile, const TerrainNode& terrain, const std::vector<Vec3>& gatePos,
                     int startGate, int aimGate, float thrustAccel, float strafeAccel, float linearDrag,
                     float hoverHeight, float maxSpeed, float stickLength)
    {
        Npc npc;
        std::vector<Material*> mats;
        Mesh* mesh = assets.load_3ds_mesh(name, meshFile, mats);
        if (!mesh) return npc; // npc.mesh stays null; caller checks before using

        npc.mesh = scene.root().create_child<MeshInstance>(name);
        npc.mesh->set_mesh(mesh);
        npc.mesh->set_materials(mats);
        npc.mesh->set_scale(0.4f);

        ParticleSystemNode* exhaust = npc.mesh->create_child<ParticleSystemNode>("npc_exhaust", 150);
        exhaust->texture = assets.loadTexture("particle_glow", "assets/textures/light.jpg");
        exhaust->blend = ParticleBlendMode::Additive;
        exhaust->set_position(0.f, mesh->bounds().min.y * 0.3f, mesh->bounds().min.z);
        exhaust->setContinuous(140.f)
            ->setShapeCircle(0.3f)
            ->setEmissionDirection(Vec3(0.f, 0.f, -1.f))
            ->setSpreadAngle(6.f)
            ->setSpeed(6.f, 10.f)
            ->setLifetime(0.25f, 0.4f)
            ->setSize(Vec2(0.6f, 0.6f), Vec2(0.1f, 0.1f))
            ->setColor(Vec4(1.f, 0.85f, 0.4f, 0.9f), Vec4(1.f, 0.25f, 0.05f, 0.f));

        npc.jet.thrustAccel = thrustAccel;
        npc.jet.strafeAccel = strafeAccel;
        npc.jet.linearDrag = linearDrag;
        npc.jet.hoverHeight = hoverHeight;
        npc.jet.maxSpeed = maxSpeed;
        npc.jet.engineForward = true;
        npc.targetGate = aimGate;

        Vec3 startPos = gatePos[(size_t)startGate];
        startPos.y = terrain.height_at(startPos.x, startPos.z) + hoverHeight + 2.f;
        npc.jet.init(startPos, stickLength);
        Vec3 aim = gatePos[(size_t)aimGate] - gatePos[(size_t)startGate];
        aim.y = 0.f;
        if (aim.length() > 1e-3f)
        {
            aim = aim.normalized();
            npc.jet.nose = npc.jet.body + aim * npc.jet.stickLength;
            npc.jet.nosePrev = npc.jet.nose;
        }
        return npc;
    }

    // steering: same left/right sign test the player's own HoverJet::update
    // uses for LEFT/RIGHT input (dot with its "right" vector), just fed by
    // "is the target gate to my left or right" instead of a key. No lap
    // tracking — it just keeps chasing the next gate in the list forever.
    void update(float dt, const TerrainNode* terrain, const std::vector<Vec3>& gatePos,
               float triggerRadius)
    {
        const int gateCount = (int)gatePos.size();
        if (gateCount == 0) return;

        Vec3 pos = jet.position();
        Vec3 toTarget = gatePos[(size_t)targetGate] - pos;
        toTarget.y = 0.f;
        if (toTarget.length() < triggerRadius * 1.5f) targetGate = (targetGate + 1) % gateCount;

        Vec3 fwd2d = jet.forward();
        fwd2d.y = 0.f;
        if (fwd2d.length() > 1e-4f) fwd2d = fwd2d.normalized();
        Vec3 dir2d = toTarget.length() > 1e-4f ? toTarget.normalized() : fwd2d;
        Vec3 right2d(-fwd2d.z, 0.f, fwd2d.x);
        float side = Vec3::Dot(dir2d, right2d);
        const float kSteerDeadzone = 0.05f;
        jet.engineRight = side > kSteerDeadzone;
        jet.engineLeft = side < -kSteerDeadzone;

        jet.update(dt, terrain);
        Vec3 p = jet.position();
        mesh->set_position(p);
        mesh->look_at(p - jet.forward());
    }
};
