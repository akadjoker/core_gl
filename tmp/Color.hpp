#pragma once


#include "Config.hpp"
 

enum PixelFormat
{
    GRAYSCALE = 1,     // 8 bit per pixel (no alpha)
    GRAY_ALPHA,        // 8*2 bpp (2 channels)
    R8G8B8,            // 24 bpp
    R8G8B8A8,          // 32 bpp    
};


class     Color 
{
public:
    Color() : r(255), g(255), b(255), a(255) {}
    Color(u8 r, u8 g, u8 b, u8 a) : r(r), g(g), b(b), a(a) {}
    Color(u8 r, u8 g, u8 b) : r(r), g(g), b(b), a(255) {}
    Color(u32 color) : r((color >> 24) & 0xFF), g((color >> 16) & 0xFF), b((color >> 8) & 0xFF), a(color & 0xFF) {}


    Color Lerp(const Color& rhs,float t) const
	{
		float invT = 1.0f - t;
		return Color(
			(u8)(r * invT + rhs.r * t),
			(u8)(g * invT + rhs.g * t),
			(u8)(b * invT + rhs.b * t),
			(u8)(a * invT + rhs.a * t)
			);
	}

	static Color FromUInt(u32 value)
	{
		u8 r = (u8)((value >> 16) & 0xFF);
		u8 g = (u8)((value >> 8) & 0xFF);
		u8 b = (u8)((value >> 0) & 0xFF);
		u8 a = (u8)((value >> 24) & 0xFF);
		return Color(r, g, b, a);
	}
	
	static Color FromFloat(float r, float g, float b, float a = 1.0f)
	{
		return Color((u8)(r * 255.0f), (u8)(g * 255.0f), (u8)(b * 255.0f), (u8)(a * 255.0f));
	}

 
	u32 ToUInt() const
	{
		return (a << 24) | (b << 16) | (g << 8) | r;
	}

    void Set(u8 r, u8 g, u8 b, u8 a)
    {
        this->r = r;
        this->g = g;
        this->b = b;
        this->a = a;
    }
    static float getRed(u32 value){return (float)((value >> 16) & 0xFF) / 255.0f;}
    static	float getGreen(u32 value)    {    return (float)((value >> 8) & 0xFF) / 255.0f;    }
    static float getBlue(u32 value)    {    return (float)((value >> 0) & 0xFF) / 255.0f;    }
    static float getAlpha(u32 value)    {    return (float)((value >> 24) & 0xFF) / 255.0f;    }


    u8 r, g, b, a;


	
	static const Color WHITE;
	
	static const Color GRAY;
	
	static const Color BLACK;
	
	static const Color RED;
	
	static const Color GREEN;
	
	static const Color BLUE;
	
	static const Color CYAN;
	
	static const Color MAGENTA;
	
	static const Color YELLOW;
	
	static const Color TRANSPARENT;
};

