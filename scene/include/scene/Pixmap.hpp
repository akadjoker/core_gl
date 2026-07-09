#pragma once

#include <coregl/gl_config.hpp>
#include "scene/Color.hpp"
#include "scene/Math.hpp"

namespace scene
{

class Pixmap
{
public:
    enum class BlendMode
    {
        copy,
        alpha,
        add,
        multiply
    };

    Pixmap();
    ~Pixmap();
    Pixmap(int w, int h, int components);
    Pixmap(int w, int h, int components, unsigned char *data);
    Pixmap(const Pixmap &image, const IntRect &crop);
    Pixmap(const Pixmap &other) = delete;
    Pixmap &operator=(const Pixmap &other) = delete;

    // Pixel operations
    void set_pixel(gl::u32 x, gl::u32 y, gl::u8 r, gl::u8 g, gl::u8 b, gl::u8 a);
    void set_pixel(gl::u32 x, gl::u32 y, gl::u32 rgba);
    gl::u32 get_pixel(gl::u32 x, gl::u32 y) const;
    Color get_pixel_color(gl::u32 x, gl::u32 y) const;

    // Fill operations
    void fill(gl::u8 r, gl::u8 g, gl::u8 b, gl::u8 a);
    void fill(gl::u32 rgba);
    void clear();

    // File operations
    bool save(const char *file_name);
    bool load(const char *file_name);
    bool load_from_memory(const unsigned char *buffer, gl::u32 bytesRead);

    // Transform operations
    void flip_vertical();
    void flip_horizontal();
    void tint(gl::u8 r, gl::u8 g, gl::u8 b);

    Pixmap* convert_to_rgba() const;
    Pixmap* resize(int newWidth, int newHeight) const;
    Pixmap* crop(const IntRect &rect) const;
    Pixmap* crop(int x, int y, int w, int h) const;
    Pixmap* crop_extended(const IntRect &rect, bool fill_transparent = true) const;

    // Drawing operations
    void draw_line(int x1, int y1, int x2, int y2, const Color &color);
    void draw_rect(int x, int y, int w, int h, const Color &color, bool fill = false);
    void draw_circle(int cx, int cy, int radius, const Color &color, bool fill = false);
    void draw_pixmap(const Pixmap &source, int x, int y);
    void draw_pixmap(const Pixmap &source, int x, int y, const IntRect &src_rect);
    void blend_pixel(gl::u32 x, gl::u32 y, const Color &color, float opacity = 1.0f, BlendMode mode = BlendMode::alpha);
    void draw_pixmap_blended(const Pixmap &source, int x, int y, float opacity = 1.0f, BlendMode mode = BlendMode::alpha);
    void draw_pixmap_blended(const Pixmap &source, int x, int y, const IntRect &src_rect, float opacity = 1.0f, BlendMode mode = BlendMode::alpha);

    // Copy operations
    void copy_region(const Pixmap &source, const IntRect &src_rect, int dst_x, int dst_y);

    // Color operations
    void replace_color(const Color &from, const Color &to, float threshold = 0.0f);
    void set_color_key(const Color &key, float threshold = 0.0f);

    // Filters
    Pixmap *apply_blur(int radius) const;
    Pixmap *apply_gaussian_blur(int radius) const;
    Pixmap *apply_sharpen() const;
    Pixmap *apply_edge_detection() const;
    Pixmap *apply_emboss() const;

    bool is_valid() const { return pixels != nullptr; }
    int get_size() const { return width * height * components; }
    bool has_alpha() const { return components == 2 || components == 4; }

    unsigned char *pixels;
    int components;
    int width;
    int height;
};

} // namespace scene
