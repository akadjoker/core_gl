#pragma once

#include "scene/Mesh.hpp"
#include "scene/Node3D.hpp"
#include <random>
#include <vector>

namespace gl
{
class Texture;
}

// CPU-simulated, data-driven emitter
// (fluent setters, the same names/behavior). Rendering differs from the
// old engine — instead of self-rendering, this node only simulates and
// bakes camera-facing billboards into a Mesh with a dynamic vertex buffer
// the SceneRenderer collects ParticleSystemNodes each frame, calls
// build_billboards(camRight, camUp) once the camera is known, and draws
// the result with its own additive/alpha blend — the node never touches
// GL directly.
enum class ParticleBlendMode
{
    Alpha,
    Additive
};

enum class EmissionMode
{
    Continuous,
    Burst,
    OneShot,
    Pulse
};
enum class EmitterShape
{
    Point,
    Sphere,
    Box,
    Cone,
    Circle,
    Ring
};
enum class EmitterState
{
    Stopped,
    Playing,
    Paused
};

struct Particle
{
    Vec3 position{0, 0, 0}, velocity{0, 0, 0}, acceleration{0, 0, 0};
    Vec4 color{1, 1, 1, 1};
    Vec2 size{1, 1};
    float rotation = 0, rotationSpeed = 0;
    Vec4 texRect{0, 0, 1, 1};
    float lifetime = 1, timeAlive = 0;
    bool active = false;
    Vec4 colorStart{1, 1, 1, 1}, colorEnd{1, 1, 1, 0};
    Vec2 sizeStart{1, 1}, sizeEnd{0.5f, 0.5f};
};

// ── Affectors: forces applied to every live particle each frame ──
class ParticleAffector
{
public:
    bool enabled = true;
    virtual ~ParticleAffector() = default;
    virtual void apply(Particle& p, float dt) = 0;
};
class GravityAffector : public ParticleAffector
{
public:
    Vec3 gravity;
    explicit GravityAffector(const Vec3& g) : gravity(g) {}
    void apply(Particle&, float) override;
};
class DragAffector : public ParticleAffector
{
public:
    float drag;
    explicit DragAffector(float d) : drag(d) {}
    void apply(Particle&, float) override;
};
class VortexAffector : public ParticleAffector
{
public:
    Vec3 center;
    float strength, radius;
    VortexAffector(const Vec3& c, float s, float r) : center(c), strength(s), radius(r) {}
    void apply(Particle&, float) override;
};
class AttractorAffector : public ParticleAffector
{
public:
    Vec3 position;
    float strength, radius;
    bool repulse;
    AttractorAffector(const Vec3& p, float s, float r, bool rep = false)
        : position(p), strength(s), radius(r), repulse(rep)
    {
    }
    void apply(Particle&, float) override;
};
class TurbulenceAffector : public ParticleAffector
{
public:
    float strength, frequency, time = 0;
    TurbulenceAffector(float s, float f) : strength(s), frequency(f) {}
    void apply(Particle&, float) override;
};
class ColorOverLifetimeAffector : public ParticleAffector
{
public:
    Vec4 startColor, endColor;
    ColorOverLifetimeAffector(const Vec4& s, const Vec4& e) : startColor(s), endColor(e) {}
    void apply(Particle&, float) override;
};
class SizeOverLifetimeAffector : public ParticleAffector
{
public:
    Vec2 startSize, endSize;
    SizeOverLifetimeAffector(const Vec2& s, const Vec2& e) : startSize(s), endSize(e) {}
    void apply(Particle&, float) override;
};

class ParticleSystemNode : public Node3D
{
public:
    static constexpr NodeType ClassType = NT_PARTICLESYSTEM;

    explicit ParticleSystemNode(const std::string& name = "particles", int maxParticles = 1000);
    ~ParticleSystemNode() override;

    bool is_a(NodeType t) const override { return t == NT_PARTICLESYSTEM || Node3D::is_a(t); }

    // ── renderer-side (game code never calls these) ──
    void simulate(float dt); // advances the simulation only
    bool ensure_gpu();       // lazily builds the dynamic Mesh
    void build_billboards(const Vec3& camRight, const Vec3& camUp); // -> Mesh VBO
    Mesh& quad_mesh() { return m_mesh; }
    int active_count() const { return m_activeCount; }

    gl::Texture* texture = nullptr; // non-owning (AssetManager)
    ParticleBlendMode blend = ParticleBlendMode::Additive;

    // ── emission ──
    ParticleSystemNode* setEmissionMode(EmissionMode m)
    {
        m_emissionMode = m;
        return this;
    }
    ParticleSystemNode* setContinuous(float pps)
    {
        m_emissionMode = EmissionMode::Continuous;
        m_emissionRate = pps;
        return this;
    }
    ParticleSystemNode* setBurst(int c, float interval = 1.f)
    {
        m_emissionMode = EmissionMode::Burst;
        m_burstCount = c;
        m_burstInterval = interval;
        return this;
    }
    ParticleSystemNode* setOneShot(int c)
    {
        m_emissionMode = EmissionMode::OneShot;
        m_oneShotCount = c;
        return this;
    }
    ParticleSystemNode* setPulse(float pps, int per)
    {
        m_emissionMode = EmissionMode::Pulse;
        m_pulseRate = pps;
        m_particlesPerPulse = per;
        return this;
    }

    // ── shape ──
    ParticleSystemNode* setShapePoint()
    {
        m_emitterShape = EmitterShape::Point;
        return this;
    }
    ParticleSystemNode* setShapeSphere(float r)
    {
        m_emitterShape = EmitterShape::Sphere;
        m_radius = r;
        return this;
    }
    ParticleSystemNode* setShapeBox(const Vec3& s)
    {
        m_emitterShape = EmitterShape::Box;
        m_boxSize = s;
        return this;
    }
    ParticleSystemNode* setShapeCone(float deg, float baseRadius = 0.f);
    ParticleSystemNode* setShapeCircle(float r)
    {
        m_emitterShape = EmitterShape::Circle;
        m_radius = r;
        return this;
    }
    ParticleSystemNode* setShapeRing(float outer, float inner)
    {
        m_emitterShape = EmitterShape::Ring;
        m_radius = outer;
        m_innerRadius = inner;
        return this;
    }

    ParticleSystemNode* setEmissionOffset(const Vec3& o)
    {
        m_emissionOffset = o;
        return this;
    }
    ParticleSystemNode* setEmissionDirection(const Vec3& d)
    {
        m_emissionDirection = d.normalized();
        return this;
    }
    ParticleSystemNode* setSpreadAngle(float deg);

    // ── per-particle properties ──
    ParticleSystemNode* setLifetime(float mn, float mx)
    {
        m_lifetimeMin = mn;
        m_lifetimeMax = mx;
        return this;
    }
    ParticleSystemNode* setLifetime(float t) { return setLifetime(t, t); }
    ParticleSystemNode* setSpeed(float mn, float mx)
    {
        m_speedMin = mn;
        m_speedMax = mx;
        return this;
    }
    ParticleSystemNode* setSpeed(float s) { return setSpeed(s, s); }
    ParticleSystemNode* setSize(const Vec2& s, const Vec2& e)
    {
        m_sizeStart = s;
        m_sizeEnd = e;
        return this;
    }
    ParticleSystemNode* setSize(float s) { return setSize(Vec2(s, s), Vec2(s, s)); }
    ParticleSystemNode* setColor(const Vec4& s, const Vec4& e)
    {
        m_colorStart = s;
        m_colorEnd = e;
        return this;
    }
    ParticleSystemNode* setColor(const Vec4& c) { return setColor(c, c); }
    ParticleSystemNode* setRotationSpeed(float mn, float mx)
    {
        m_rotSpeedMin = mn;
        m_rotSpeedMax = mx;
        return this;
    }

    // ── physics / control ──
    ParticleSystemNode* setGravity(const Vec3& g)
    {
        m_gravity = g;
        return this;
    }
    ParticleSystemNode* setDrag(float d)
    {
        m_drag = d;
        return this;
    }
    ParticleSystemNode* setDuration(float s)
    {
        m_duration = s;
        return this;
    }
    ParticleSystemNode* setLoop(bool l)
    {
        m_loop = l;
        return this;
    }

    // ── atlas ──
    ParticleSystemNode* setAtlasGrid(int cols, int rows);
    void clearAtlas()
    {
        m_atlasFrames.clear();
        m_useAtlas = false;
    }

    // ── playback ──
    void play();
    void stop();
    void pause() { m_state = EmitterState::Paused; }
    void emitBurst(int count);
    EmitterState getState() const { return m_state; }

    // ── affectors (owned) ──
    ParticleSystemNode* addGravity(const Vec3& g)
    {
        m_affectors.push_back(new GravityAffector(g));
        return this;
    }
    ParticleSystemNode* addDrag(float s)
    {
        m_affectors.push_back(new DragAffector(s));
        return this;
    }
    ParticleSystemNode* addVortex(const Vec3& c, float s, float r)
    {
        m_affectors.push_back(new VortexAffector(c, s, r));
        return this;
    }
    ParticleSystemNode* addAttractor(const Vec3& p, float s, float r, bool rep = false)
    {
        m_affectors.push_back(new AttractorAffector(p, s, r, rep));
        return this;
    }
    ParticleSystemNode* addTurbulence(float s, float f)
    {
        m_affectors.push_back(new TurbulenceAffector(s, f));
        return this;
    }
    ParticleSystemNode* addColorOverLifetime(const Vec4& s, const Vec4& e)
    {
        m_affectors.push_back(new ColorOverLifetimeAffector(s, e));
        return this;
    }
    ParticleSystemNode* addSizeOverLifetime(const Vec2& s, const Vec2& e)
    {
        m_affectors.push_back(new SizeOverLifetimeAffector(s, e));
        return this;
    }
    void clearAffectors();

protected:
    void _ready() override { play(); }
    void _update(float dt) override { simulate(dt); }
    void _release_gpu() override { m_mesh.release_gpu(); }

private:
    void emitContinuous(float dt);
    void emitBurstMode(float dt);
    void emitPulseMode(float dt);
    Particle* getFreeParticle();
    void initParticle(Particle* p);
    Vec3 calcEmissionPosition();
    Vec3 calcEmissionVelocity();
    void updateParticles(float dt);
    void applyAffectors(float dt);
    float rnd(float a, float b);
    Vec3 randomUnitVector();

    std::vector<Particle> m_particles;
    std::vector<ParticleAffector*> m_affectors;
    Mesh m_mesh; // dynamic VBO (4 verts/particle) + static IBO (quads)
    bool m_gpu_ready = false;
    int m_maxParticles;
    int m_activeCount = 0;

    EmissionMode m_emissionMode = EmissionMode::Continuous;
    EmitterShape m_emitterShape = EmitterShape::Point;
    EmitterState m_state = EmitterState::Stopped;
    Vec3 m_emissionOffset{0, 0, 0}, m_emissionDirection{0, 1, 0};
    float m_emissionRate = 10, m_burstInterval = 1;
    int m_burstCount = 10, m_oneShotCount = 10;
    float m_pulseRate = 1;
    int m_particlesPerPulse = 5;
    std::vector<Vec4> m_atlasFrames;
    bool m_useAtlas = false;
    int m_currentFrame = 0;
    float m_radius = 1, m_innerRadius = 0.5f, m_coneAngle = 0.785f;
    Vec3 m_boxSize{1, 1, 1};
    float m_lifetimeMin = 1, m_lifetimeMax = 3, m_speedMin = 1, m_speedMax = 5;
    Vec2 m_sizeStart{0.5f, 0.5f}, m_sizeEnd{0.1f, 0.1f};
    Vec4 m_colorStart{1, 1, 1, 1}, m_colorEnd{1, 1, 1, 0};
    float m_rotSpeedMin = 0, m_rotSpeedMax = 0, m_spreadAngle = 0;
    Vec3 m_gravity{0, 0, 0};
    float m_drag = 0;
    float m_duration = -1;
    bool m_loop = true;
    float m_emissionTimer = 0, m_timeAlive = 0, m_burstTimer = 0, m_pulseTimer = 0;
    bool m_hasEmittedOneShot = false;
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist01{0, 1};
};
