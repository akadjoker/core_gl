#include "scene/Behavior.hpp"
#include "scene/Node3D.hpp"

Behavior::Behavior(const std::string& name) : Node(name) { m_type = NT_BEHAVIOR; }

Node3D* Behavior::get_target() const
{
    Node* p = get_parent();
    while (p)
    {
        if (Node3D* n = p->as<Node3D>()) return n;
        p = p->get_parent();
    }
    return nullptr;
}

void Behavior::_update(float dt)
{
    if (!enabled) return;
    if (Node3D* target = get_target()) on_process(*target, dt);
}
