#pragma once

#include "scene/Math.hpp"

namespace gl
{
class Texture;
}

// Surface appearance for the built-in forward pass. Plain data: the
// SceneRenderer reads it and sets the shader state — a Material never
// touches GL itself. Pointers are non-owning (textures usually come from
// the AssetManager).
class Material
{
public:
    Material() = default;
    explicit Material(const Vec3& color) : base_color(color) {}

    Vec3 base_color = Vec3(1.f, 1.f, 1.f); // multiplied with the diffuse map
    gl::Texture* diffuse = nullptr;        // null = plain base_color
    // detail map: tiled `detail_scale` times over the uv and multiplied in
    // (mid-gray = neutral) — close-up texture for terrain
    gl::Texture* detail = nullptr;
    float detail_scale = 40.f;

    bool double_sided = false; // disables backface culling for this item
    bool unlit = false;        // skip lighting: output the flat color/texture

    float specular = 0.f; // 0 = matte; blinn-phong strength
    float shininess = 32.f;
};
