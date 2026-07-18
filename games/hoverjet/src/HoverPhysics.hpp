#pragma once

// Hovercraft flight model for the hoverjet demo 


#include <scene/Math.hpp>
#include <scene/TerrainNode.hpp>
#include <SDL2/SDL.h>
#include <cmath>

struct HoverJet
{
    Vec3 body, bodyPrev;
    Vec3 nose, nosePrev;
    float stickLength = 7.f;

    // input state (mirrors the lua jet.ENGINE_*/BOOST_ON flags)
    bool engineForward = true;
    bool engineBack = false;
    bool engineLeft = false;
    bool engineRight = false;
    bool boost = false;

    // tuning (real units: m/s^2, seconds). Without maxSpeed, top speed would
    // just be the thrustAccel/linearDrag drag equilibrium (~92 normal, ~220
    // boosted at these numbers) — maxSpeed below is a hard clamp on top of
    // that, mainly there to cap boosted flight so a big dt step can't punch
    // the craft through a thin bit of terrain.
    float thrustAccel = 55.f;
    float boostMul = 2.4f;
    float reverseAccel = 20.f;
    float strafeAccel = 34.f;
    float gravity = 14.f;
    float linearDrag = 0.6f; // fraction of velocity/sec removed
    float hoverHeight = 2.f; // clearance kept above the terrain surface
    float maxSpeed = 550.f;  // hard cap, applied after integration below
    float brakeRate = 6.f;   // DOWN: exponential decay rate (1/s) of actual velocity toward 0

    void init(const Vec3& startPos, float len = 5.f)
    {
        stickLength = len;
        body = startPos;
        nose = startPos + Vec3(0.f, 0.f, -len);
        bodyPrev = body;
        nosePrev = nose;
    }

    void handle_key(SDL_Scancode sc, bool down)
    {
        if (sc == SDL_SCANCODE_UP) engineForward = down;
        else if (sc == SDL_SCANCODE_DOWN) engineBack = down;
        else if (sc == SDL_SCANCODE_LEFT) engineLeft = down;
        else if (sc == SDL_SCANCODE_RIGHT) engineRight = down;
        else if (sc == SDL_SCANCODE_SPACE) boost = down;
    }

    Vec3 forward() const
    {
        Vec3 d = nose - body;
        float len = d.length();
        return len > 1e-5f ? d * (1.f / len) : Vec3(0.f, 0.f, -1.f);
    }

    void clampSpeed(const Vec3& pos, Vec3& prevPos) const
    {
        Vec3 vel = pos - prevPos;
        float sp = vel.length();
        if (sp > maxSpeed) prevPos = pos - vel * (maxSpeed / sp);
    }

    // height_at(x,z) is a point ON the terrain surface directly below/above
    // `pos`; the signed distance from pos to the tangent plane through that
    // point, measured along the surface normal, tells us how deep inside
    // the hover clearance we are — same math as PHPatchedObstruction.
    // `prevPos` is nudged so next frame's implied Verlet velocity
    // (pos - prevPos) has no component driving back into the surface —
    // without this, continuous thrust re-penetrates a wall every frame
    // and the craft jitters in place instead of sliding along it.
    void resolveTerrainContact(Vec3& pos, Vec3& prevPos, const TerrainNode* terrain) const
    {
        if (!terrain) return;
        Vec3 n = terrain->normal_at(pos.x, pos.z);
        Vec3 surfacePoint(pos.x, terrain->height_at(pos.x, pos.z), pos.z);
        float dist = Vec3::Dot(n, pos - surfacePoint);
        if (dist >= hoverHeight) return;
        pos += n * (hoverHeight - dist);
        float vn = Vec3::Dot(pos - prevPos, n);
        if (vn < 0.f) prevPos += n * vn;
    }

    void update(float dt, const TerrainNode* terrain)
    {
        if (dt <= 0.f) return;

        Vec3 fwd = forward();
        // standard cross(forward, worldUp): +X when facing -Z, i.e. the
        // craft's actual right (the lua's z1-z0,x0-x1 pair is this negated —
        // porting its sign as-is is what had LEFT/RIGHT swapped before).
        Vec3 right(-fwd.z, 0.f, fwd.x);

        // ── forces (only the NOSE gets thrust/strafe — see file header) ──
        Vec3 noseAccel(0.f, -gravity, 0.f);
        if (engineForward) noseAccel += fwd * (boost ? thrustAccel * boostMul : thrustAccel);
        else if (engineBack) noseAccel -= fwd * reverseAccel;
        if (engineLeft) noseAccel -= right * strafeAccel;
        else if (engineRight) noseAccel += right * strafeAccel;

        Vec3 bodyAccel(0.f, -gravity, 0.f);

        // ── verlet integrate (damped) ──
        const float damp = 1.f - linearDrag * dt;
        Vec3 newBody = body + (body - bodyPrev) * damp + bodyAccel * (dt * dt);
        Vec3 newNose = nose + (nose - nosePrev) * damp + noseAccel * (dt * dt);
        bodyPrev = body;
        nosePrev = nose;
        body = newBody;
        nose = newNose;

        // ── terrain contact (see resolveTerrainContact above) ──
        resolveTerrainContact(body, bodyPrev, terrain);
        resolveTerrainContact(nose, nosePrev, terrain);

        // ── rigid stick constraint: keep body/nose exactly stickLength apart ──
        Vec3 d = nose - body;
        float len = d.length();
        if (len > 1e-5f)
        {
            float diff = (len - stickLength) / len;
            Vec3 correction = d * (0.5f * diff);
            body += correction;
            nose -= correction;
        }

        // ── DOWN = brake: exponential decay of the *actual* velocity
        // (both points, whatever direction they're moving) toward 0,
        // instead of just a small counter-thrust along the nose axis that
        // barely dents a fast-moving craft. This is what makes holding
        // DOWN reliably bring the craft to a stop instead of just slowing
        // the approach to equilibrium speed. ──
        if (engineBack)
        {
            float decay = expf(-brakeRate * dt);
            bodyPrev = body - (body - bodyPrev) * decay;
            nosePrev = nose - (nose - nosePrev) * decay;
        }

        // ── clamp to maxSpeed: LAST, not right after integration — both
        // resolveTerrainContact and the stick constraint above can still
        // move body/nose by an arbitrarily large amount (e.g. the craft
        // wedged somewhere the terrain correction and the stick correction
        // fight each other every frame) without touching bodyPrev/nosePrev,
        // so clamping only right after integration doesn't actually bound
        // the worst case — it just hides it one step earlier. Caught here
        // this reads as a hard-capped speed instead of runaway jitter
        // (position vibrating wildly while the ship goes nowhere). No
        // min-speed clamp exists (or makes sense) here — the craft can sit
        // still with the engine off. ──
        clampSpeed(body, bodyPrev);
        clampSpeed(nose, nosePrev);
    }

    Vec3 position() const { return (body + nose) * 0.5f; }
    float speed(float dt) const
    {
        if (dt <= 0.f) return 0.f;
        return ((body + nose) * 0.5f - (bodyPrev + nosePrev) * 0.5f).length() / dt;
    }
};

// Sphere-sphere separation between two ships (player-vs-NPC, NPC-vs-NPC):
// same "push apart along the delta" trick as everything else in this file,
// just between two moving bodies instead of a body and static terrain.
// Both of each ship's points (body AND bodyPrev, nose AND nosePrev) shift
// by the identical push vector — a rigid translation, not a stretch — so
// this can't introduce the runaway-velocity failure mode
// resolveTerrainContact's own comment describes: translating pos and
// prevPos together leaves (pos - prevPos) exactly as it was, so a ship
// grazing another just slides off with its speed intact instead of
// bouncing or jittering. `radius` is per ship — two ships collide once the
// distance between their centers drops under the sum of their radii.
// returns true when the pair actually overlapped this call (a hit, not
// just a check) — callers use that edge to trigger a one-shot sound/effect
// without needing their own separate distance test.
inline bool resolveShipCollision(HoverJet& a, HoverJet& b, float radius)
{
    Vec3 pa = a.position(), pb = b.position();
    Vec3 delta = pb - pa;
    float dist = delta.length();
    const float minDist = radius * 2.f;
    if (dist >= minDist) return false;
    Vec3 n = dist > 1e-3f ? delta * (1.f / dist) : Vec3(1.f, 0.f, 0.f);
    Vec3 push = n * ((minDist - dist) * 0.5f);

    a.body -= push;
    a.bodyPrev -= push;
    a.nose -= push;
    a.nosePrev -= push;
    b.body += push;
    b.bodyPrev += push;
    b.nose += push;
    b.nosePrev += push;
    return true;
}
