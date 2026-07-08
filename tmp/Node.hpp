#pragma once

#include "Config.hpp"
#include <string>
#include <vector>

// Tipo concreto de cada node. Substitui os virtuais asNode3D()/asMeshNode()
// do core antigo: get_type() para o tipo exacto, is_a() para downcast seguro.
// Novos tipos (Camera3D, MeshInstance, Sprite3D, Camera2D, Action, Controller...)
// entram aqui à medida dos milestones.
enum NodeType : u16
{
    NT_NODE = 0,
    NT_NODE3D,
    NT_CAMERA3D,
    NT_MESHINSTANCE,
    NT_SKINNEDMESH,
    NT_BONEATTACHMENT,
    NT_LIGHT,
    NT_BEHAVIOR,
    NT_PARTICLESYSTEM,
    NT_COUNT
};

// Base da árvore de cena: hierarquia + ciclo de vida. NÃO tem transform
// (Actions/Timers/Controllers herdam daqui). Node3D acrescenta o transform.
// Posse: o pai é dono dos filhos e destrói-os; remove_child() devolve a posse.
class Node
{
public:
    static constexpr NodeType ClassType = NT_NODE;

    explicit Node(const std::string &name = "Node");
    virtual ~Node();

    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;

    // --- Tipo ---
    NodeType get_type() const { return m_type; }
    virtual bool is_a(NodeType t) const { return t == NT_NODE; }

    template <typename T> T *as()
    {
        return is_a(T::ClassType) ? static_cast<T *>(this) : nullptr;
    }
    template <typename T> const T *as() const
    {
        return is_a(T::ClassType) ? static_cast<const T *>(this) : nullptr;
    }

    // --- Identidade ---
    const std::string &get_name() const { return m_name; }
    void set_name(const std::string &name) { m_name = name; }

    // --- Hierarquia ---
    Node *add_child(Node *child);   // toma posse do filho
    void remove_child(Node *child); // destaca sem destruir (posse volta ao chamador)
    void remove_from_parent();      // destaca-se do pai (sem se destruir)

    Node *get_parent() const { return m_parent; }
    Node *get_child(size_t index) const;
    size_t get_child_count() const { return m_children.size(); }
    const std::vector<Node *> &get_children() const { return m_children; }

    Node *find_child(const std::string &name, bool recursive = true) const;
    bool is_ancestor_of(const Node *node) const;

    // --- Ciclo de vida (propagação na árvore) ---
    void propagate_ready();           // filhos primeiro, depois o próprio _ready()
    void propagate_update(float dt);  // o próprio _update(), depois os filhos

    // Engine-interno: um antepassado mexeu no transform; propaga para os
    // descendentes. Público porque é chamado em Node* base ao longo da árvore;
    // não chamar a partir de código de jogo. Node3D faz override.
    virtual void on_parent_transform_changed();

protected:
    // Hooks de subclasse (mapeiam 1:1 para o binding ZenPy).
    virtual void _ready() {}
    virtual void _update(float dt) { (void)dt; }

    NodeType m_type;
    Node *m_parent;
    std::vector<Node *> m_children;

private:
    void detach_child(Node *child);

    std::string m_name;
    bool m_ready_called;
};
