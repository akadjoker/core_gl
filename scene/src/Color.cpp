#include "scene/Color.hpp"

#include <algorithm>
#include <cmath>

namespace scene
{

const gl::u32 Color::White       = 0xFFFFFFFF;
const gl::u32 Color::Black       = 0xFF000000;
const gl::u32 Color::Red         = 0xFFFF0000;
const gl::u32 Color::Green       = 0xFF00FF00;
const gl::u32 Color::Blue        = 0xFF0000FF;
const gl::u32 Color::Yellow      = 0xFFFFFF00;
const gl::u32 Color::Cyan        = 0xFF00FFFF;
const gl::u32 Color::Magenta     = 0xFFFF00FF;
const gl::u32 Color::Orange      = 0xFFFFA500;
const gl::u32 Color::Gray        = 0xFF808080;
const gl::u32 Color::Transparent = 0x00000000;

static inline int iclamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float fclamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

Color::Color(gl::u8 r, gl::u8 g, gl::u8 b, gl::u8 a)
{
    m_value = (gl::u32(a) << 24) | (gl::u32(r) << 16) | (gl::u32(g) << 8) | gl::u32(b);
}

void Color::setRGB(gl::u8 R, gl::u8 G, gl::u8 B)
{
    m_value = (m_value & 0xFF000000) | (gl::u32(R) << 16) | (gl::u32(G) << 8) | gl::u32(B);
}

void Color::setRGBA(gl::u8 R, gl::u8 G, gl::u8 B, gl::u8 A)
{
    m_value = (gl::u32(A) << 24) | (gl::u32(R) << 16) | (gl::u32(G) << 8) | gl::u32(B);
}

Color Color::withAlpha(float a) const
{
    gl::u32 alpha;
    if (a <= 0.0f)
        alpha = 0;
    else if (a >= 1.0f)
        alpha = 0xFF;
    else
        alpha = gl::u32(0xFF * a);

    return Color((m_value & 0x00FFFFFF) | (alpha << 24));
}

float Color::getHue() const
{
    int h = r();
    int s = g();
    int v = b();

    int maxV = std::max(h, std::max(s, v));
    int minV = std::min(h, std::min(s, v));

    float hue = 0.0f;

    if (maxV == minV)
        hue = 0.0f;
    else if (maxV == h)
        hue = std::fmod(60.0f * (s - v) / (maxV - minV) + 360.0f, 360.0f);
    else if (maxV == s)
        hue = 60.0f * (v - h) / (maxV - minV) + 120.0f;
    else if (maxV == v)
        hue = 60.0f * (h - s) / (maxV - minV) + 240.0f;

    return hue / 360.0f;
}

float Color::getSaturation() const
{
    int h = r();
    int s = g();
    int v = b();

    int maxV = std::max(h, std::max(s, v));
    if (maxV == 0) return 0.0f;

    int minV = std::min(h, std::min(s, v));
    return float(maxV - minV) / float(maxV);
}

float Color::getValue() const
{
    int h = r();
    int s = g();
    int v = b();
    return std::max(h, std::max(s, v)) / 255.0f;
}

float Color::getLuminance() const
{
    return 0.2126f * red() + 0.7152f * green() + 0.0722f * blue();
}

Color Color::lerp(const Color& to, float t) const
{
    return Color::lerp(*this, to, t);
}

Color Color::multiply(const Color& other) const
{
    return Color::fromRGBFloat(red() * other.red(),
                               green() * other.green(),
                               blue() * other.blue(),
                               alpha());
}

gl::u32 Color::toARGB(float alpha) const
{
    alpha = fclamp(alpha, 0.0f, 1.0f);
    return (gl::u32(0xFF * alpha) << 24) | (m_value & 0x00FFFFFF);
}

Color Color::fromRGB(gl::u8 r, gl::u8 g, gl::u8 b, gl::u8 a)
{
    return Color(r, g, b, a);
}

Color Color::fromRGBFloat(float r, float g, float b, float a)
{
    auto intColor = [](float v) -> gl::u8 {
        return (gl::u8)iclamp((int)(v * 256.0f), 0, 255);
    };
    return Color(intColor(r), intColor(g), intColor(b), intColor(a));
}

Color Color::fromHSV(float h, float s, float v, float a)
{
    h = std::fmod(h * 360.0f, 360.0f);
    if (h < 0.0f) h += 360.0f;

    int hi = (int)(h / 60.0f) % 6;
    float f = h / 60.0f - std::floor(h / 60.0f);
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);

    float rf = 0, gf = 0, bf = 0;
    switch (hi)
    {
        case 0: rf = v; gf = t; bf = p; break;
        case 1: rf = q; gf = v; bf = p; break;
        case 2: rf = p; gf = v; bf = t; break;
        case 3: rf = p; gf = q; bf = v; break;
        case 4: rf = t; gf = p; bf = v; break;
        case 5: rf = v; gf = p; bf = q; break;
    }

    gl::u8 R = (gl::u8)(rf * 255.0f);
    gl::u8 G = (gl::u8)(gf * 255.0f);
    gl::u8 B = (gl::u8)(bf * 255.0f);
    gl::u8 A = (gl::u8)(fclamp(a, 0.0f, 1.0f) * 255.0f);
    return Color(R, G, B, A);
}

Color Color::lerp(Color from, Color to, float t)
{
    if (t <= 0.0f) return from;
    if (t >= 1.0f) return to;

    int a = from.a();
    int r = from.r();
    int g = from.g();
    int b = from.b();

    int dA = (int)to.a() - a;
    int dR = (int)to.r() - r;
    int dG = (int)to.g() - g;
    int dB = (int)to.b() - b;

    a += (int)(dA * t);
    r += (int)(dR * t);
    g += (int)(dG * t);
    b += (int)(dB * t);

    return Color((gl::u8)r, (gl::u8)g, (gl::u8)b, (gl::u8)a);
}

} // namespace scene