#include "scene/OscillateBehavior.hpp"
#include "scene/Node3D.hpp"
#include <cmath>

OscillateBehavior::OscillateBehavior(const std::string& name) : Behavior(name) {}

void OscillateBehavior::_ready()
{
    if (Node3D* t = get_target())
    {
        m_start = t->get_position();
        m_haveStart = true;
    }
}

void OscillateBehavior::on_process(Node3D& target, float dt)
{
    if (!m_haveStart)
    {
        m_start = target.get_position();
        m_haveStart = true;
    }
    m_t += dt;
    float s = sinf(m_t * m_frequency * 6.2831853f) * m_amplitude;
    target.set_position(m_start + m_axis * s);
}
