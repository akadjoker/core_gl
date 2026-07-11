#pragma once

#include <string>
#include <utility>
#include <vector>

using u16 = unsigned short;

enum NodeType : unsigned short
{
    NT_NODE = 0,
    NT_NODE3D,
    NT_CAMERA3D,
    NT_MESHINSTANCE,
    NT_SKINNEDMESH,
    NT_BONEATTACHMENT,
    NT_LIGHT,
    NT_POINTLIGHT,
    NT_SPOTLIGHT,
    NT_BEHAVIOR,
    NT_PARTICLESYSTEM,
    NT_DECALSYSTEM,
    NT_GRASSSYSTEM,
    NT_WATER,
    NT_OCEAN,
    NT_TERRAIN,
    NT_TILEDTERRAIN,
    NT_INFINITETERRAIN,
    NT_TERRAINLOD,
    NT_RIBBONTRAIL,
    NT_TERRAINPAGING,
    NT_LENSFLARE,
    NT_COUNT
};

class Node
{
public:
    static constexpr NodeType ClassType = NT_NODE;

    explicit Node(const std::string& name = "Node");
    virtual ~Node();

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // --- Tipo ---
    NodeType get_type() const { return m_type; }
    virtual bool is_a(NodeType t) const { return t == NT_NODE; }

    template <typename T> T* as() { return is_a(T::ClassType) ? static_cast<T*>(this) : nullptr; }
    template <typename T> const T* as() const
    {
        return is_a(T::ClassType) ? static_cast<const T*>(this) : nullptr;
    }

    // --- Identidade ---
    const std::string& get_name() const { return m_name; }
    void set_name(const std::string& name) { m_name = name; }

    // --- Hierarquia ---
    // Cria um filho já na árvore — sem new nem cast no código do jogo:
    //   Camera3D* cam = scene.root().create_child<Camera3D>("fly");
    // A memória pertence ao pai (como em add_child): destruir o pai destrói
    // a subárvore inteira.
    template <typename T, typename... Args> T* create_child(Args&&... args)
    {
        T* child = new T(std::forward<Args>(args)...);
        add_child(child);
        return child;
    }

    Node* add_child(Node* child);   // toma posse do filho
    void remove_child(Node* child); // destaca sem destruir (posse volta ao chamador)
    void remove_from_parent();      // destaca-se do pai (sem se destruir)

    Node* get_parent() const { return m_parent; }
    Node* get_child(size_t index) const;
    size_t get_child_count() const { return m_children.size(); }
    const std::vector<Node*>& get_children() const { return m_children; }

    Node* find_child(const std::string& name, bool recursive = true) const;
    bool is_ancestor_of(const Node* node) const;

    // --- Ciclo de vida (propagação na árvore) ---
    void propagate_ready();          // filhos primeiro, depois o próprio _ready()
    void propagate_update(float dt); // o próprio _update(), depois os filhos
    void propagate_release_gpu();    // liberta objetos GL na subárvore inteira

    virtual void on_parent_transform_changed();

protected:
    virtual void _ready() {}
    virtual void _update(float dt) { (void)dt; }
    // nodes com lado GPU (water targets, etc.) libertam-no aqui; chamado por
    // propagate_release_gpu enquanto o contexto GL ainda está vivo
    virtual void _release_gpu() {}

    NodeType m_type;
    Node* m_parent;
    std::vector<Node*> m_children;

private:
    void detach_child(Node* child);

    std::string m_name;
    bool m_ready_called;
};
