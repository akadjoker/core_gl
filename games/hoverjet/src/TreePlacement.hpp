#pragma once

// Reads tree spawn zones from a hand-painted marker image (red blobs on a
// copy of the heightmap, same resolution/alignment — a pixel here must
// land on the same map cell it does in the heightmap) instead of scattering
// trees uniformly at random across the whole terrain, which is what put
// trees on top of the track/cliffs in odd spots with no regard for what's
// actually underneath. A human painting patches already knows where a
// forest should be; this just fills those patches in.
//
// Each red blob is its own forest patch: bigger blobs seed proportionally
// more trees (area / minAreaPerTree, minimum 1 so even a small dot marker
// gets a tree), and each tree's exact spot is a random pixel sampled from
// *inside that blob's own flood-filled shape* — not its bounding box — so
// odd/oval/concave painted patches come out looking like the shape that
// was actually painted, not a rectangle.

#include <scene/Math.hpp>
#include <scene/Pixmap.hpp>
#include <scene/TerrainNode.hpp>
#include <algorithm>
#include <random>
#include <utility>
#include <vector>

namespace treeplacement
{

inline std::vector<Vec3> load_from_image(const char* path, const TerrainNode& terrain,
                                         float terrainSize, float minAreaPerTree = 30.f,
                                         unsigned seed = 99)
{
    std::vector<Vec3> out;
    scene::Pixmap img;
    if (!img.load(path)) return out;
    const int w = img.width, h = img.height;
    if (w < 2 || h < 2) return out;

    auto isRed = [&](int x, int y) {
        scene::Color c = img.get_pixel_color((gl::u32)x, (gl::u32)y);
        return c.r() > 160 && c.g() < 90 && c.b() < 90;
    };

    std::vector<gl::u8> visited((size_t)w * h, 0);
    std::mt19937 rng(seed);
    std::vector<std::pair<int, int>> stack, blob;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            size_t idx = (size_t)y * w + x;
            if (visited[idx] || !isRed(x, y)) continue;

            blob.clear();
            stack.clear();
            stack.push_back(std::make_pair(x, y));
            visited[idx] = 1;
            while (!stack.empty())
            {
                int cx = stack.back().first, cy = stack.back().second;
                stack.pop_back();
                blob.push_back(std::make_pair(cx, cy));
                static const int dx[4] = {1, -1, 0, 0};
                static const int dy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d)
                {
                    int nx = cx + dx[d], ny = cy + dy[d];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    size_t nidx = (size_t)ny * w + nx;
                    if (visited[nidx] || !isRed(nx, ny)) continue;
                    visited[nidx] = 1;
                    stack.push_back(std::make_pair(nx, ny));
                }
            }

            int count = (int)((float)blob.size() / minAreaPerTree);
            if (count < 1) count = 1;
            if (count > (int)blob.size()) count = (int)blob.size();
            std::shuffle(blob.begin(), blob.end(), rng);
            for (int i = 0; i < count; ++i)
            {
                float px = (float)blob[(size_t)i].first, py = (float)blob[(size_t)i].second;
                float wx = px / (float)(w - 1) * terrainSize;
                float wz = py / (float)(h - 1) * terrainSize;
                float wy = terrain.height_at(wx, wz);
                out.push_back(Vec3(wx, wy, wz));
            }
        }
    }
    return out;
}

} // namespace treeplacement
