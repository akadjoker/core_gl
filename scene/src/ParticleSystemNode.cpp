#include "scene/ParticleSystemNode.hpp"
#include <cmath>

static constexpr float kPI = 3.14159265358979323846f;
static constexpr float kTwoPI = kPI * 2.f;

static Vec4 lerp4(const Vec4& a, const Vec4& b, float t)
{
    return Vec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
               a.w + (b.w - a.w) * t);
}
static Vec2 lerp2(const Vec2& a, const Vec2& b, float t)
{
    return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

// ── Affectors ──
void GravityAffector::apply(Particle& p, float)
{
    if (enabled) p.acceleration += gravity;
}
void DragAffector::apply(Particle& p, float dt)
{
    if (enabled) p.velocity = p.velocity * (1.f - drag * dt);
}
void VortexAffector::apply(Particle& p, float)
{
    if (!enabled) return;
    Vec3 toC = center - p.position;
    float d = toC.length();
    if (d < radius && d > 0.001f)
    {
        Vec3 dir = toC * (1.f / d);
        Vec3 tan(-dir.z, 0.f, dir.x);
        p.acceleration += tan * (strength * (1.f - d / radius));
    }
}
void AttractorAffector::apply(Particle& p, float)
{
    if (!enabled) return;
    Vec3 to = position - p.position;
    float d = to.length();
    if (d < radius && d > 0.001f)
        p.acceleration += (to * (1.f / d)) * (strength * (1.f - d / radius) * (repulse ? -1.f : 1.f));
}
void TurbulenceAffector::apply(Particle& p, float dt)
{
    if (!enabled) return;
    time += dt;
    p.acceleration += Vec3(sinf(p.position.x * frequency + time) * strength,
                           sinf(p.position.y * frequency + time * 1.3f) * strength,
                           cosf(p.position.z * frequency + time * 0.7f) * strength);
}
void ColorOverLifetimeAffector::apply(Particle& p, float)
{
    if (enabled) p.color = lerp4(startColor, endColor, p.timeAlive / p.lifetime);
}
void SizeOverLifetimeAffector::apply(Particle& p, float)
{
    if (enabled) p.size = lerp2(startSize, endSize, p.timeAlive / p.lifetime);
}

// ── ParticleSystemNode ──

ParticleSystemNode::ParticleSystemNode(const std::string& name, int maxParticles)
    : Node3D(name), m_maxParticles(maxParticles), m_rng(std::random_device{}())
{
    m_type = NT_PARTICLESYSTEM;
    m_particles.resize((size_t)maxParticles);
}

ParticleSystemNode::~ParticleSystemNode()
{
    clearAffectors();
}

void ParticleSystemNode::clearAffectors()
{
    for (ParticleAffector* a : m_affectors)
        delete a;
    m_affectors.clear();
}

ParticleSystemNode* ParticleSystemNode::setShapeCone(float deg, float baseRadius)
{
    m_emitterShape = EmitterShape::Cone;
    m_coneAngle = deg * (kPI / 180.f);
    m_radius = baseRadius;
    return this;
}
ParticleSystemNode* ParticleSystemNode::setSpreadAngle(float deg)
{
    m_spreadAngle = deg * (kPI / 180.f);
    return this;
}
ParticleSystemNode* ParticleSystemNode::setAtlasGrid(int cols, int rows)
{
    m_atlasFrames.clear();
    float cw = 1.f / cols, ch = 1.f / rows;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            m_atlasFrames.push_back(Vec4(c * cw, r * ch, cw, ch));
    m_useAtlas = true;
    return this;
}

void ParticleSystemNode::play()
{
    m_state = EmitterState::Playing;
    if (m_emissionMode == EmissionMode::OneShot && !m_hasEmittedOneShot)
    {
        emitBurst(m_oneShotCount);
        m_hasEmittedOneShot = true;
    }
}
void ParticleSystemNode::stop()
{
    m_state = EmitterState::Stopped;
    for (Particle& p : m_particles)
        p.active = false;
    m_activeCount = 0;
}

void ParticleSystemNode::simulate(float dt)
{
    if (m_state == EmitterState::Paused) return;
    m_timeAlive += dt;
    if (m_state == EmitterState::Playing)
    {
        switch (m_emissionMode)
        {
        case EmissionMode::Continuous:
            emitContinuous(dt);
            break;
        case EmissionMode::Burst:
            emitBurstMode(dt);
            break;
        case EmissionMode::Pulse:
            emitPulseMode(dt);
            break;
        case EmissionMode::OneShot:
            break;
        }
        if (m_duration > 0.f && m_timeAlive >= m_duration)
        {
            if (m_loop)
            {
                m_timeAlive = 0.f;
                m_hasEmittedOneShot = false;
            }
            else
                stop();
        }
    }
    updateParticles(dt);
    applyAffectors(dt);
    m_activeCount = 0;
    for (const Particle& p : m_particles)
        if (p.active) ++m_activeCount;
}

void ParticleSystemNode::emitContinuous(float dt)
{
    m_emissionTimer += dt;
    float interval = 1.f / m_emissionRate;
    int count = 0;
    while (m_emissionTimer >= interval)
    {
        ++count;
        m_emissionTimer -= interval;
        if (count > 100)
        {
            m_emissionTimer = 0;
            break;
        }
    }
    for (int i = 0; i < count; ++i)
    {
        Particle* p = getFreeParticle();
        if (p) initParticle(p);
    }
}
void ParticleSystemNode::emitBurstMode(float dt)
{
    m_burstTimer += dt;
    if (m_burstTimer >= m_burstInterval)
    {
        emitBurst(m_burstCount);
        m_burstTimer = 0;
        if (!m_loop) stop();
    }
}
void ParticleSystemNode::emitPulseMode(float dt)
{
    m_pulseTimer += dt;
    float interval = 1.f / m_pulseRate;
    if (m_pulseTimer >= interval)
    {
        emitBurst(m_particlesPerPulse);
        m_pulseTimer = 0;
    }
}
void ParticleSystemNode::emitBurst(int count)
{
    for (int i = 0; i < count; ++i)
    {
        Particle* p = getFreeParticle();
        if (p) initParticle(p);
    }
}

Particle* ParticleSystemNode::getFreeParticle()
{
    for (Particle& p : m_particles)
        if (!p.active) return &p;
    Particle* oldest = nullptr;
    float maxProg = 0.9f;
    for (Particle& p : m_particles)
    {
        float prog = p.timeAlive / p.lifetime;
        if (prog > maxProg)
        {
            maxProg = prog;
            oldest = &p;
        }
    }
    return oldest;
}

void ParticleSystemNode::initParticle(Particle* p)
{
    p->position = calcEmissionPosition();
    p->velocity = calcEmissionVelocity();
    p->acceleration = m_gravity;
    p->lifetime = rnd(m_lifetimeMin, m_lifetimeMax);
    p->timeAlive = 0;
    p->sizeStart = m_sizeStart;
    p->sizeEnd = m_sizeEnd;
    p->size = m_sizeStart;
    p->colorStart = m_colorStart;
    p->colorEnd = m_colorEnd;
    p->color = m_colorStart;
    p->rotation = rnd(0, kTwoPI);
    p->rotationSpeed = rnd(m_rotSpeedMin, m_rotSpeedMax);
    p->texRect = (m_useAtlas && !m_atlasFrames.empty())
                     ? m_atlasFrames[m_currentFrame % (int)m_atlasFrames.size()]
                     : Vec4(0, 0, 1, 1);
    p->active = true;
}

Vec3 ParticleSystemNode::calcEmissionPosition()
{
    const Mat4& world = get_world_matrix();
    Vec4 wp4 = world * Vec4(m_emissionOffset.x, m_emissionOffset.y, m_emissionOffset.z, 1.f);
    Vec3 wp(wp4.x, wp4.y, wp4.z);
    Vec3 off(0, 0, 0);
    switch (m_emitterShape)
    {
    case EmitterShape::Point:
        break;
    case EmitterShape::Sphere:
    {
        Vec3 dir = randomUnitVector();
        float r = cbrtf(m_dist01(m_rng)) * m_radius;
        off = dir * r;
        break;
    }
    case EmitterShape::Box:
        off = Vec3(rnd(-m_boxSize.x * .5f, m_boxSize.x * .5f),
                   rnd(-m_boxSize.y * .5f, m_boxSize.y * .5f),
                   rnd(-m_boxSize.z * .5f, m_boxSize.z * .5f));
        break;
    case EmitterShape::Cone:
    case EmitterShape::Circle:
    {
        if (m_radius > 0.f)
        {
            float a = rnd(0, kTwoPI);
            float r = sqrtf(m_dist01(m_rng)) * m_radius;
            off = global_right() * (cosf(a) * r) + global_up() * (sinf(a) * r);
        }
        break;
    }
    case EmitterShape::Ring:
    {
        float a = rnd(0, kTwoPI);
        float r = rnd(m_innerRadius, m_radius);
        off = global_right() * (cosf(a) * r) + global_up() * (sinf(a) * r);
        break;
    }
    }
    if (Vec3::Dot(off, off) > 0.0001f)
    {
        Vec4 o4 = world * Vec4(off.x, off.y, off.z, 0.f);
        wp += Vec3(o4.x, o4.y, o4.z);
    }
    return wp;
}

Vec3 ParticleSystemNode::calcEmissionVelocity()
{
    float speed = rnd(m_speedMin, m_speedMax);
    Vec4 d4 = get_world_matrix() *
              Vec4(m_emissionDirection.x, m_emissionDirection.y, m_emissionDirection.z, 0.f);
    Vec3 dir = Vec3(d4.x, d4.y, d4.z).normalized();
    if (m_spreadAngle > 0.f)
    {
        float theta = rnd(0, kTwoPI), phi = rnd(0, m_spreadAngle);
        Vec3 u(0, 1, 0);
        if (fabsf(Vec3::Dot(dir, u)) > 0.99f) u = Vec3(1, 0, 0);
        Vec3 r2 = Vec3::Cross(dir, u).normalized();
        u = Vec3::Cross(r2, dir).normalized();
        dir = (dir * cosf(phi) + (r2 * cosf(theta) + u * sinf(theta)) * sinf(phi)).normalized();
    }
    return dir * speed;
}

void ParticleSystemNode::updateParticles(float dt)
{
    for (Particle& p : m_particles)
    {
        if (!p.active) continue;
        p.timeAlive += dt;
        if (p.timeAlive >= p.lifetime)
        {
            p.active = false;
            continue;
        }
        p.velocity += p.acceleration * dt;
        if (m_drag > 0.f) p.velocity = p.velocity * (1.f - m_drag * dt);
        p.position += p.velocity * dt;
        p.rotation += p.rotationSpeed * dt;
        float t = p.timeAlive / p.lifetime;
        p.color = lerp4(p.colorStart, p.colorEnd, t);
        p.size = lerp2(p.sizeStart, p.sizeEnd, t);
        p.acceleration = m_gravity;
    }
}

void ParticleSystemNode::applyAffectors(float dt)
{
    if (m_affectors.empty()) return;
    for (Particle& p : m_particles)
    {
        if (!p.active) continue;
        for (ParticleAffector* a : m_affectors)
            if (a && a->enabled) a->apply(p, dt);
    }
}

bool ParticleSystemNode::ensure_gpu()
{
    if (m_gpu_ready) return true;

    // static index buffer: two triangles per particle quad
    std::vector<MeshVertex> verts((size_t)m_maxParticles * 4u);
    std::vector<u32> indices((size_t)m_maxParticles * 6u);
    for (int q = 0; q < m_maxParticles; ++q)
    {
        u32 base = (u32)(q * 4);
        u32* idx = &indices[(size_t)q * 6];
        idx[0] = base;
        idx[1] = base + 1;
        idx[2] = base + 2;
        idx[3] = base;
        idx[4] = base + 2;
        idx[5] = base + 3;
    }
    m_mesh.set_data(verts.data(), (u32)verts.size(), indices.data(), (u32)indices.size());
    m_mesh.upload_dynamic();
    m_gpu_ready = true;
    return true;
}

// Bakes active particles into camera-facing quads. MeshVertex has no color
// field, so — for this billboard-only mesh — the unused tangent (vec4)
// carries RGBA color instead; the particle shader reads it as such. This
// keeps Mesh's layout untouched for every other user of it.
void ParticleSystemNode::build_billboards(const Vec3& camRight, const Vec3& camUp)
{
    if (!ensure_gpu()) return;

    static std::vector<MeshVertex> verts;
    verts.assign((size_t)m_maxParticles * 4u, MeshVertex{});
    int q = 0;
    for (const Particle& p : m_particles)
    {
        if (!p.active || q >= m_maxParticles) continue;
        Vec3 pr = camRight, pu = camUp;
        if (p.rotation != 0.f)
        {
            float c = cosf(p.rotation), s = sinf(p.rotation);
            Vec3 nr = pr * c - pu * s, nu = pr * s + pu * c;
            pr = nr;
            pu = nu;
        }
        Vec3 hr = pr * (p.size.x * .5f), hu = pu * (p.size.y * .5f);
        float u0 = p.texRect.x, v0 = p.texRect.y;
        float u1 = p.texRect.x + p.texRect.z, v1 = p.texRect.y + p.texRect.w;
        MeshVertex* v = &verts[(size_t)q * 4];
        v[0].position = p.position - hr - hu;
        v[0].uv = Vec2(u0, v0);
        v[0].tangent = p.color;
        v[1].position = p.position + hr - hu;
        v[1].uv = Vec2(u1, v0);
        v[1].tangent = p.color;
        v[2].position = p.position + hr + hu;
        v[2].uv = Vec2(u1, v1);
        v[2].tangent = p.color;
        v[3].position = p.position - hr + hu;
        v[3].uv = Vec2(u0, v1);
        v[3].tangent = p.color;
        ++q;
    }

    // particles are baked in WORLD space already (calcEmissionPosition uses
    // the node's world matrix); the mesh itself carries an identity transform
    m_mesh.update_vertices(verts.data(), (u32)verts.size());
}

float ParticleSystemNode::rnd(float a, float b)
{
    return a + m_dist01(m_rng) * (b - a);
}
Vec3 ParticleSystemNode::randomUnitVector()
{
    float z = rnd(-1, 1), a = rnd(0, kTwoPI), r = sqrtf(1 - z * z);
    return Vec3(r * cosf(a), r * sinf(a), z);
}
