#pragma once

#include "scene/Math.hpp"
#include <vector>

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
    bool lightmapped = false;  // BSP: modulate diffuse * detail using uv2

    // BSP shader-script surfaces (surfaceparm trans / a non-opaque
    // blendFunc stage — see BSPLoader.cpp's shaderInfoMap): drawn in a
    // second, blended pass with depth write off instead of the normal
    // opaque pass. `additive` picks GL_ONE/GL_ONE instead of
    // SRC_ALPHA/ONE_MINUS_SRC_ALPHA (flames/glows vs glass/fences).
    bool blend = false;
    bool additive = false;

    // BSP shader-script "animMap <fps> <frame1> <frame2> ..." (torches,
    // conduits, ...) — see BSPLoader.cpp's shaderInfoMap. Empty = not
    // animated; `diffuse` is just the current frame, swapped in place by
    // update_material_animations() below instead of a separate "which
    // frame" field the renderer would need to know about.
    std::vector<gl::Texture*> anim_frames;
    float anim_fps = 0.f;

    float specular = 0.f; // 0 = matte; blinn-phong strength
    float shininess = 32.f;
};

// Advances every animated material's `diffuse` to whatever frame
// `totalTimeSeconds` lands on. Call once per frame with a running clock
// (not dt — frame selection needs the absolute elapsed time, not a delta)
// for each material list that might contain animated ones, e.g.
// update_material_animations(bspInstance->get_materials(), appTime).
inline void update_material_animations(const std::vector<Material*>& materials, float totalTimeSeconds)
{
    for (Material* m : materials)
    {
        if (!m || m->anim_frames.empty() || m->anim_fps <= 0.f) continue;
        int frameCount = (int)m->anim_frames.size();
        int idx = (int)(totalTimeSeconds * m->anim_fps) % frameCount;
        if (idx < 0) idx += frameCount;
        m->diffuse = m->anim_frames[idx];
    }
}
