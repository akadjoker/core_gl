#include "scene/Node.hpp"

#include <algorithm>

Node::Node(const std::string& name)
    : m_type(NT_NODE), m_parent(nullptr), m_name(name), m_ready_called(false)
{
}

Node::~Node()
{
    // the parent owns its children: destroying a node destroys the subtree
    for (Node* child : m_children)
    {
        child->m_parent = nullptr; // skip detach bookkeeping, we're going away
        delete child;
    }
    m_children.clear();

    if (m_parent) m_parent->detach_child(this);
}

Node* Node::add_child(Node* child)
{
    if (!child || child == this) return child;
    if (child->is_ancestor_of(this)) return child; // would create a cycle

    if (child->m_parent) child->m_parent->detach_child(child);
    child->m_parent = this;
    m_children.push_back(child);

    // a new parent means a new world transform for the subtree
    child->on_parent_transform_changed();
    return child;
}

void Node::remove_child(Node* child)
{
    if (!child || child->m_parent != this) return;
    detach_child(child);
    child->m_parent = nullptr;
    child->on_parent_transform_changed();
}

void Node::remove_from_parent()
{
    if (m_parent) m_parent->remove_child(this);
}

Node* Node::get_child(size_t index) const
{
    return index < m_children.size() ? m_children[index] : nullptr;
}

Node* Node::find_child(const std::string& name, bool recursive) const
{
    for (Node* child : m_children)
        if (child->m_name == name) return child;

    if (recursive)
    {
        for (Node* child : m_children)
        {
            Node* found = child->find_child(name, true);
            if (found) return found;
        }
    }
    return nullptr;
}

bool Node::is_ancestor_of(const Node* node) const
{
    for (const Node* p = node ? node->m_parent : nullptr; p; p = p->m_parent)
        if (p == this) return true;
    return false;
}

void Node::propagate_ready()
{
    // children first, then self — a node's _ready() can rely on its subtree
    for (Node* child : m_children)
        child->propagate_ready();

    if (!m_ready_called)
    {
        m_ready_called = true;
        _ready();
    }
}

void Node::propagate_update(float dt)
{
    _update(dt);
    for (Node* child : m_children)
        child->propagate_update(dt);
}

void Node::propagate_release_gpu()
{
    for (Node* child : m_children)
        child->propagate_release_gpu();
    _release_gpu();
}

void Node::on_parent_transform_changed()
{
    // plain Node has no transform of its own, but 3D descendants deeper in
    // the tree still need to hear about it
    for (Node* child : m_children)
        child->on_parent_transform_changed();
}

void Node::detach_child(Node* child)
{
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) m_children.erase(it);
}
