#pragma once

// Arcade flight model for the Zero fighter 
#include <scene/Math.hpp>
#include <scene/TerrainNode.hpp>

struct FlightPhysics
{
    Vec3 position{0.f, 0.f, 0.f};
    Quaternion orientation;
    Vec3 velocity{0.f, 0.f, 0.f}; // world-space

    float throttle = 0.6f;   // 0..1
    float pitchInput = 0.f;  // -1..1, +1 = nose up
    float rollInput = 0.f;   // -1..1, +1 = bank right
    float yawInput = 0.f;    // -1..1, +1 = yaw right (rudder)

    // tuning — cruise speed at throttle t is roughly sqrt(t*maxThrustAccel/linearDrag);
    // at throttle 0.6 that's ~85 units/s now (was ~37), full throttle ~155
    float maxThrustAccel = 95.f;   // units/s^2 at full throttle
    float pitchRate = 1.1f;        // rad/s at full stick
    float rollRate = 2.6f;         // rad/s at full stick
    float yawRate = 0.6f;          // rad/s at full rudder
    float liftPerSpeed = 0.85f;    // lift accel per unit of forward speed
    float minFlySpeed = 18.f;      // below this, lift falls off (stall)
    float linearDrag = 0.0105f;    // opposes velocity, scales with speed^2
    float gravity = 18.f;
    float turnBleed = 0.18f;       // banked lift bleeds off as speed sideways drag
    float velocityAlign = 2.2f;    // how fast velocity re-aligns toward nose (weathercocking)
    float maxSpeed = 320.f;

    void init(const Vec3& startPos, const Quaternion& startOrientation, float startSpeed)
    {
        position = startPos;
        orientation = startOrientation;
        velocity = orientation.rotateVector(Vec3(0.f, 0.f, -1.f)) * startSpeed;
        throttle = 0.6f;
        pitchInput = rollInput = yawInput = 0.f;
    }

    Vec3 forward() const { return orientation.rotateVector(Vec3(0.f, 0.f, -1.f)); }
    Vec3 up() const { return orientation.rotateVector(Vec3(0.f, 1.f, 0.f)); }
    Vec3 right() const { return orientation.rotateVector(Vec3(1.f, 0.f, 0.f)); }

    float speed() const { return velocity.length(); }

    void update(float dt)
    {
        // ── attitude: stick deflection torques the airframe about its own
        // body axes (pitch/roll/yaw) — same convention as the Lua demo's
        // elevator/aileron/rudder. rotateAxisAngle post-multiplies
        // (q' = q * delta), which is the standard body-fixed/intrinsic
        // composition ONLY when the axis is given in local/body space
        // (exactly how Quaternion's own rotatePitch/rotateRoll/rotateYaw
        // use it: constant Vec3(1,0,0) etc, never a world-space vector).
        // Passing world-space forward()/right()/up() here was the bug —
        // each axis was already rotated by the current orientation, so
        // composing pitch and roll in the same frame corrupted into an
        // uncontrolled twist instead of two independent body rotations.
        orientation.rotateAxisAngle(Vec3(1.f, 0.f, 0.f), pitchInput * pitchRate * dt);  // local right
        orientation.rotateAxisAngle(Vec3(0.f, 0.f, 1.f), -rollInput * rollRate * dt);   // local forward axis
        orientation.rotateAxisAngle(Vec3(0.f, 1.f, 0.f), -yawInput * yawRate * dt);     // local up
        orientation.normalize();

        Vec3 fwd = forward();
        Vec3 top = up();
        float spd = velocity.length();

        // thrust: straight down the nose
        Vec3 accel = fwd * (throttle * maxThrustAccel);

        // lift: grows with forward speed, falls off hard below stall speed,
        // banking (top() tilted away from world-up) trades lift for turn —
        // exactly what "pull back while banked" does in a real dogfight
        float liftMag = spd * liftPerSpeed;
        if (spd < minFlySpeed) liftMag *= spd / minFlySpeed;
        accel += top * liftMag;

        // banked turn: pulling lift sideways bleeds a bit of forward speed
        // off, so tight turns cost energy instead of being free
        float bank = 1.f - top.y; // 0 level, up to ~2 fully inverted
        accel -= fwd * (bank * spd * turnBleed);

        // gravity, always world-down
        accel += Vec3(0.f, -gravity, 0.f);

        // drag opposes current velocity, grows with speed
        if (spd > 0.001f) accel -= velocity.normalized() * (spd * spd * linearDrag);

        velocity += accel * dt;

        // weathercocking: velocity gradually re-aligns toward the nose so
        // the plane doesn't drift sideways forever like a brick with wings
        Vec3 targetVel = fwd * velocity.length();
        velocity = velocity.lerp(targetVel, 1.f - expf(-velocityAlign * dt));

        float vs = velocity.length();
        if (vs > maxSpeed) velocity *= (maxSpeed / vs);

        position += velocity * dt;
    }
};
