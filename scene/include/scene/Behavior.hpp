#pragma once

#include "scene/Node.hpp"

class Node3D;

// Base of the controller nodes: a transform-less node that runs every frame
// and writes the transform of its first Node3D ancestor (the "target"). This
// is how navigation gets attached to a camera or object —
// add_child(new FreeFlyBehavior()). Switching modes = swapping/disabling the
// child (enabled).
// Ported from tmp/core/include/Behavior.hpp.
class Behavior : public Node
{
public:
    static constexpr NodeType ClassType = NT_BEHAVIOR;

    explicit Behavior(const std::string& name = "Behavior");

    bool is_a(NodeType t) const override { return t == NT_BEHAVIOR || Node::is_a(t); }

    bool enabled = true;

    // First Node3D ancestor in the tree (nullptr if none).
    Node3D* get_target() const;

protected:
    // Implemented by concrete controllers. Only called when enabled and a
    // target exists. Don't touch GL here — just read input and write the
    // transform.
    virtual void on_process(Node3D& target, float dt) = 0;

private:
    // Seals the lifecycle hook: manages enabled/target and delegates to on_process.
    void _update(float dt) final;
};
