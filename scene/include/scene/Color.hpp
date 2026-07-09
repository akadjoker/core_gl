#pragma once

#include <coregl/gl_config.hpp>

namespace scene
{

// 32-bit color packed as 0xAARRGGBB (ARGB), matching HaxePunk's Color abstract.
// Provides HSV <-> RGB conversions, lerp, luminance and multiply.
class Color
{
public:
    static const gl::u32 White;
    static const gl::u32 Black;
    static const gl::u32 Red;
    static const gl::u32 Green;
    static const gl::u32 Blue;
    static const gl::u32 Yellow;
    static const gl::u32 Cyan;
    static const gl::u32 Magenta;
    static const gl::u32 Orange;
    static const gl::u32 Gray;
    static const gl::u32 Transparent;

    Color() : m_value(0xFFFFFFFF) {}
    Color(gl::u32 argb) : m_value(argb) {}
    Color(gl::u8 r, gl::u8 g, gl::u8 b, gl::u8 a = 255);

    gl::u32 value() const { return m_value; }
    void setValue(gl::u32 v) { m_value = v; }

    gl::u8 r() const { return (gl::u8)((m_value >> 16) & 0xFF); }
    gl::u8 g() const { return (gl::u8)((m_value >> 8) & 0xFF); }
    gl::u8 b() const { return (gl::u8)(m_value & 0xFF); }
    gl::u8 a() const { return (gl::u8)((m_value >> 24) & 0xFF); }

    float red() const { return r() / 255.0f; }
    float green() const { return g() / 255.0f; }
    float blue() const { return b() / 255.0f; }
    float alpha() const { return a() / 255.0f; }

    void setRGB(gl::u8 R, gl::u8 G, gl::u8 B);
    void setRGBA(gl::u8 R, gl::u8 G, gl::u8 B, gl::u8 A);
    Color withAlpha(float a) const;

    float getHue() const;
    float getSaturation() const;
    float getValue() const;
    float getLuminance() const;

    Color lerp(const Color& to, float t) const;
    Color multiply(const Color& other) const;

    gl::u32 toARGB(float alpha) const;

    static Color fromRGB(gl::u8 r, gl::u8 g, gl::u8 b, gl::u8 a = 255);
    static Color fromRGBFloat(float r, float g, float b, float a = 1.0f);
    static Color fromHSV(float h, float s, float v, float a = 1.0f);
    static Color lerp(Color from, Color to, float t);

    bool operator==(const Color& o) const { return m_value == o.m_value; }
    bool operator!=(const Color& o) const { return m_value != o.m_value; }
    operator gl::u32() const { return m_value; }

private:
    gl::u32 m_value;
};

} // namespace scene