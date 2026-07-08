#pragma once

using u32 = unsigned int;
using u16 = unsigned short;
#ifndef DEBUG_BREAK_IF
#define DEBUG_BREAK_IF(cond) ((void)0)
#endif

#include <cmath>

const unsigned int MaxUInt32 = 0xFFFFFFFF;
const int MinInt32 = 0x80000000;
const int MaxInt32 = 0x7FFFFFFF;
const float MaxFloat = 3.402823466e+38F;
const float MinPosFloat = 1.175494351e-38F;

const float Pi = 3.141592654f;
const float TwoPi = 6.283185307f;
const float PiHalf = 1.570796327f;

const float Epsilon = 0.000001f;
const float ZeroEpsilon =
    32.0f * MinPosFloat; // Very small epsilon for checking against 0.0f

const float M_INFINITY = 1.0e30f;

#define powi(base, exp) (int)powf((float)(base), (float)(exp))

#define ToRadians(x) (float)(((x) * Pi / 180.0f))
#define ToDegrees(x) (float)(((x) * 180.0f / Pi))

inline float Sin(float a) { return sin(a * Pi / 180); }
inline float Cos(float a) { return cos(a * Pi / 180); }
inline float Tan(float a) { return tan(a * Pi / 180); }
inline float SinRad(float a) { return sin(a); }
inline float CosRad(float a) { return cos(a); }
inline float TanRad(float a) { return tan(a); }
inline float ASin(float a) { return asin(a) * 180 / Pi; }
inline float ACos(float a) { return acos(a) * 180 / Pi; }
inline float ATan(float a) { return atan(a) * 180 / Pi; }
inline float ATan2(float y, float x) { return atan2(y, x) * 180 / Pi; }
inline float ASinRad(float a) { return asin(a); }
inline float ACosRad(float a) { return acos(a); }
inline float ATanRad(float a) { return atan(a); }
inline float ATan2Rad(float y, float x) { return atan2(y, x); }
inline int Floor(float a) { return (int)(floor(a)); }
inline int Ceil(float a) { return (int)(ceil(a)); }
inline int Trunc(float a)
{
    if (a > 0)
        return Floor(a);
    else
        return Ceil(a);
}
inline int Round(float a)
{
    if (a < 0)
        return (int)(ceil(a - 0.5f));
    else
        return (int)(floor(a + 0.5f));
}
inline float Sqrt(float a)
{
    if (a > 0)
        return sqrt(a);
    else
        return 0;
}
inline float Abs(float a)
{
    if (a < 0)
        a = -a;
    return a;
}
inline int Mod(int a, int b)
{
    if (b == 0)
        return 0;
    return a % b;
}
inline float FMod(float a, float b)
{
    if (b == 0)
        return 0;
    return fmod(a, b);
}
inline float Pow(float a, float b) { return pow(a, b); }
inline int Sign(float a)
{
    if (a < 0)
        return -1;
    else if (a > 0)
        return 1;
    else
        return 0;
}
inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline int Min(int a, int b) { return a < b ? a : b; }
inline int Max(int a, int b) { return a > b ? a : b; }
inline float Clamp(float a, float min, float max)
{
    if (a < min)
        a = min;
    else if (a > max)
        a = max;
    return a;
}
inline int Clamp(int a, int min, int max)
{
    if (a < min)
        a = min;
    else if (a > max)
        a = max;
    return a;
}
static inline float Clamp1(float x)
{
    return x < -1.f ? -1.f : (x > 1.f ? 1.f : x);
}

enum NoInitHint
{
    NO_INIT
};

inline float degToRad(float f) { return f * 0.017453293f; }

inline float radToDeg(float f) { return f * 57.29577951f; }

// -------------------------------------------------------------------------------------------------
// Vector
// -------------------------------------------------------------------------------------------------

struct Vec2
{

    float x, y;

    // ------------
    // Constructors
    // ------------
    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(const float v) : x(v), y(v) {}

    explicit Vec2(NoInitHint)
    {
        // Constructor without default initialization
    }

    Vec2(const float x, const float y) : x(x), y(y) {}

    void set(float x, float y)
    {
        this->x = x;
        this->y = y;
    }

    // ------
    // Access
    // ------
    float operator[](unsigned int index) const { return *(&x + index); }

    float &operator[](unsigned int index) { return *(&x + index); }

    // -----------
    // Comparisons
    // -----------
    bool operator==(const Vec2 &v) const
    {
        return (x > v.x - Epsilon && x < v.x + Epsilon && y > v.y - Epsilon && y < v.y + Epsilon);
    }

    bool operator!=(const Vec2 &v) const
    {
        return (x < v.x - Epsilon || x > v.x + Epsilon || y < v.y - Epsilon || y > v.y + Epsilon);
    }

    // ---------------------
    // Arithmetic operations
    // ---------------------
    Vec2 operator-() const { return Vec2(-x, -y); }

    Vec2 operator+(const Vec2 &v) const { return Vec2(x + v.x, y + v.y); }

    Vec2 &operator+=(const Vec2 &v) { return *this = *this + v; }

    Vec2 operator-(const Vec2 &v) const { return Vec2(x - v.x, y - v.y); }

    Vec2 &operator-=(const Vec2 &v) { return *this = *this - v; }

    Vec2 operator*(const float f) const { return Vec2(x * f, y * f); }

    Vec2 &operator*=(const float f) { return *this = *this * f; }

    Vec2 operator/(const float f) const { return Vec2(x / f, y / f); }

    Vec2 &operator/=(const float f) { return *this = *this / f; }

    // ----------------
    // Special products
    // ----------------
    float dot(const Vec2 &v) const { return x * v.x + y * v.y; }

    // ----------------
    // Other operations
    // ----------------
    float length() const { return sqrtf(x * x + y * y); }

    Vec2 normalized() const
    {
        float invLen = 1.0f / length();
        return Vec2(x * invLen, y * invLen);
    }

    void normalize()
    {
        float invLen = 1.0f / length();
        x *= invLen;
        y *= invLen;
    }

    Vec2 lerp(const Vec2 &v, float f) const
    {
        return Vec2(x + (v.x - x) * f, y + (v.y - y) * f);
    }
};

struct Vec3
{

    float x, y, z;

    // --- CTORS ---
    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    explicit Vec3(NoInitHint) {}                 // sem init
    explicit Vec3(float v) : x(v), y(v), z(v) {} // cuidado: implícitos de float!
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

    void set(float X, float Y, float Z)
    {
        x = X;
        y = Y;
        z = Z;
    }

    // --- Access ---
    float operator[](unsigned i) const { return *(&x + i); } // sem bounds-check
    float &operator[](unsigned i) { return *(&x + i); }

    // --- Comparisons (usa a tua constante Epsilon) ---
    bool operator==(const Vec3 &v) const
    {
        return (x > v.x - Epsilon && x < v.x + Epsilon && y > v.y - Epsilon && y < v.y + Epsilon && z > v.z - Epsilon && z < v.z + Epsilon);
    }
    bool operator!=(const Vec3 &v) const { return !(*this == v); }

    // --- Arithmetic ---
    Vec3 operator-() const { return Vec3(-x, -y, -z); }

    Vec3 operator+(const Vec3 &v) const
    {
        return Vec3(x + v.x, y + v.y, z + v.z);
    }
    Vec3 &operator+=(const Vec3 &v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }

    Vec3 operator-(const Vec3 &v) const
    {
        return Vec3(x - v.x, y - v.y, z - v.z);
    }
    Vec3 &operator-=(const Vec3 &v)
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }

    Vec3 operator*(float f) const { return Vec3(x * f, y * f, z * f); }
    Vec3 &operator*=(float f)
    {
        x *= f;
        y *= f;
        z *= f;
        return *this;
    }

    Vec3 operator/(float f) const
    {
// evita /0 silenciosa
#ifdef DEBUG
        if (f == 0.0f)
        {
            DEBUG_BREAK_IF(true);
            return *this;
        }
#endif
        float inv = 1.0f / f;
        return Vec3(x * inv, y * inv, z * inv);
    }
    Vec3 &operator/=(float f)
    {
#ifdef DEBUG
        if (f == 0.0f)
        {
            DEBUG_BREAK_IF(true);
            return *this;
        }
#endif
        float inv = 1.0f / f;
        x *= inv;
        y *= inv;
        z *= inv;
        return *this;
    }

    // --- Special products ---
    float dot(const Vec3 &v) const { return x * v.x + y * v.y + z * v.z; }
    Vec3 cross(const Vec3 &v) const
    {
        return Vec3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
    }

    // --- Length / distance ---
    float length() const { return sqrtf(x * x + y * y + z * z); }
    float length_squared() const { return x * x + y * y + z * z; }

    float distance(const Vec3 &v) const { return (*this - v).length(); }
    float distance_squared(const Vec3 &v) const
    {
        return (*this - v).length_squared();
    }

    // --- Normalize ---
    Vec3 normalized() const
    {
        float len2 = length_squared();
        if (len2 <= 0.0f)
            return Vec3(0.0f, 0.0f, 0.0f);
        float invLen = 1.0f / sqrtf(len2);
        return Vec3(x * invLen, y * invLen, z * invLen);
    }
    void normalize()
    {
        float len2 = length_squared();
        if (len2 <= 0.0f)
        {
            x = y = z = 0.0f;
            return;
        }
        float invLen = 1.0f / sqrtf(len2);
        x *= invLen;
        y *= invLen;
        z *= invLen;
    }

    // --- Lerp ---
    Vec3 lerp(const Vec3 &v, float t) const
    {
        return Vec3(x + (v.x - x) * t, y + (v.y - y) * t, z + (v.z - z) * t);
    }
    static Vec3 Lerp(const Vec3 &a, const Vec3 &b, float t)
    {
        return a.lerp(b, t);
    }

    // --- Helpers ---
    Vec3 Min(const Vec3 &v) const
    {
        return Vec3(x < v.x ? x : v.x, y < v.y ? y : v.y, z < v.z ? z : v.z);
    }
    Vec3 Max(const Vec3 &v) const
    {
        return Vec3(x > v.x ? x : v.x, y > v.y ? y : v.y, z > v.z ? z : v.z);
    }

    static Vec3 Clamp(const Vec3 &v, const Vec3 &mn, const Vec3 &mx)
    {
        return Vec3(v.x < mn.x ? mn.x : (v.x > mx.x ? mx.x : v.x),
                    v.y < mn.y ? mn.y : (v.y > mx.y ? mx.y : v.y),
                    v.z < mn.z ? mn.z : (v.z > mx.z ? mx.z : v.z));
    }

    static Vec3 Cross(const Vec3 &a, const Vec3 &b) { return a.cross(b); }
    static float Dot(const Vec3 &a, const Vec3 &b) { return a.dot(b); }

    static Vec3 Sub(const Vec3 &a, const Vec3 &b) { return a - b; }
    static Vec3 Add(const Vec3 &a, const Vec3 &b) { return a + b; }

    static Vec3 Normalize(const Vec3 &v) { return v.normalized(); }

    static float DistanceFromSq(const Vec3 &a, const Vec3 &b)
    {
        return (a - b).length_squared();
    }
    static float DistanceFrom(const Vec3 &a, const Vec3 &b)
    {
        return (a - b).length();
    }
    static float Length(const Vec3 &v) { return v.length(); }

    // clamp para acosf
    static float Clamp1(float x)
    {
        return (x < -1.0f) ? -1.0f : (x > 1.0f) ? 1.0f
                                                : x;
    }

    // ângulos por vértice (weights)
    static Vec3 GetAngleWeights(const Vec3 &v, const Vec3 &v1, const Vec3 &v2)
    {
        const float a2 = DistanceFromSq(v1, v2), as = sqrtf(a2); // oposto a v
        const float b2 = DistanceFromSq(v, v2), bs = sqrtf(b2);  // oposto a v1
        const float c2 = DistanceFromSq(v, v1), cs = sqrtf(c2);  // oposto a v2
        const float eps = 1e-12f;
        if (as < eps || bs < eps || cs < eps)
            return Vec3(1.0f);
        const float cosA = Clamp1((b2 + c2 - a2) / (2.f * bs * cs));
        const float cosB = Clamp1((a2 + c2 - b2) / (2.f * as * cs));
        const float cosC = Clamp1((a2 + b2 - c2) / (2.f * as * bs));
        return Vec3(acosf(cosA), acosf(cosB), acosf(cosC));
    }
};

inline Vec3 operator*(float s, const Vec3 &v) { return v * s; }

struct Vec4
{
    float x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(0) {}

    Vec4(float v) : x(v), y(v), z(v), w(v) {}
    explicit Vec4(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) {}
    explicit Vec4(Vec3 v) : x(v.x), y(v.y), z(v.z), w(1.0f) {}
    explicit Vec4(Vec3 v, float W) : x(v.x), y(v.y), z(v.z), w(W) {}

    float operator[](unsigned i) const { return *(&x + i); }
    float &operator[](unsigned i) { return *(&x + i); }

    Vec4 operator+(const Vec4 &v) const
    {
        return Vec4(x + v.x, y + v.y, z + v.z, w + v.w);
    }
    Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }
    Vec4 operator*(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
    Vec4 operator/(float d) const
    {
        DEBUG_BREAK_IF(d == 0);
        return Vec4(x / d, y / d, z / d, w / d);
    }

    Vec4 &operator+=(const Vec4 &v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        w += v.w;
        return *this;
    }
    Vec4 &operator-=(const Vec4 &v)
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        w -= v.w;
        return *this;
    }
    Vec4 &operator*=(float s)
    {
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }

    // Integração com Vec3 (afeta só xyz; w preserva)
    Vec4 operator+(const Vec3 &v) const
    {
        return Vec4(x + v.x, y + v.y, z + v.z, w);
    }
    Vec4 operator-(const Vec3 &v) const
    {
        return Vec4(x - v.x, y - v.y, z - v.z, w);
    }
    Vec4 &operator+=(const Vec3 &v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }
    Vec4 &operator-=(const Vec3 &v)
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }

    Vec3 xyz() const { return Vec3{x, y, z}; }
    void setXYZ(const Vec3 &v)
    {
        x = v.x;
        y = v.y;
        z = v.z;
    }
};

// Simétrico: float * Vec4
inline Vec4 operator*(float s, const Vec4 &v) { return v * s; }

// -------------------------------------------------------------------------------------------------
// Quaternion
// -------------------------------------------------------------------------------------------------

struct Quaternion
{

    float x, y, z, w;

    // ------------
    // Constructors
    // ------------
    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}

    explicit Quaternion(const float x, const float y, const float z,
                        const float w)
        : x(x), y(y), z(z), w(w)
    {
    }

    Quaternion(const float eulerX, const float eulerY, const float eulerZ)
    {
        Quaternion roll(sinf(eulerX * 0.5f), 0, 0, cosf(eulerX * 0.5f));
        Quaternion pitch(0, sinf(eulerY * 0.5f), 0, cosf(eulerY * 0.5f));
        Quaternion yaw(0, 0, sinf(eulerZ * 0.5f), cosf(eulerZ * 0.5f));

        // Order: y * x * z
        *this = pitch * roll * yaw;
    }

    void ToAxisAngle(Vec3 &axis, float &angle) const
    {
        Quaternion qn = Normalize(*this);
        angle = 2.0f * acosf(qn.w);
        float s = sqrtf(1.0f - qn.w * qn.w);
        if (s < 1e-8f)
            axis = Vec3(1, 0, 0);
        else
            axis = Vec3(qn.x / s, qn.y / s, qn.z / s);
    }

    // ---------------------
    // Arithmetic operations
    // ---------------------
    Quaternion operator*(const Quaternion &q) const
    {
        return Quaternion(y * q.z - z * q.y + q.x * w + x * q.w,
                          z * q.x - x * q.z + q.y * w + y * q.w,
                          x * q.y - y * q.x + q.z * w + z * q.w,
                          w * q.w - (x * q.x + y * q.y + z * q.z));
    }

    Quaternion &operator*=(const Quaternion &q) { return *this = *this * q; }

    void normalize()
    {
        float invLen = 1.0f / length();
        x *= invLen;
        y *= invLen;
        z *= invLen;
        w *= invLen;
    }

    void identity()
    {
        x = y = z = 0.0f;
        w = 1.0f;
    }

    float length() const { return sqrtf(x * x + y * y + z * z + w * w); }

    void rotate(const float x, const float y, const float z)
    {
        *this = *this * Quaternion(x, y, z, 0) * Quaternion(-x, -y, -z, 0);
    }

    void rotate(const Vec3 &rotation)
    {
        *this = *this * Quaternion(rotation.x, rotation.y, rotation.z, 0) * Quaternion(-rotation.x, -rotation.y, -rotation.z, 0);
    }

    void rotate(float angle, float ax, float ay, float az)
    {
        *this = *this * Quaternion(angle, ax, ay, az) * Quaternion(-angle, -ax, -ay, -az);
    }

    void rotateAxisAngle(const Vec3 &axis, float angle)
    {
        *this *= FromAxisAngle(axis, angle);
        normalize();
    }
    void rotateYaw(float yaw) { rotateAxisAngle(Vec3(0, 1, 0), yaw); }
    void rotatePitch(float pit) { rotateAxisAngle(Vec3(1, 0, 0), pit); }
    void rotateRoll(float rol) { rotateAxisAngle(Vec3(0, 0, 1), rol); }

    // --- De Euler / Para Euler ---
    void setEuler(float ex, float ey, float ez)
    {
        *this = Quaternion(ex, ey, ez);
    }
    void setEuler(const Vec3 &e) { setEuler(e.x, e.y, e.z); }

    static inline float clamp1(float v)
    {
        return v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
    }
    Vec3 getEuler() const
    {
        // Y*X*Z
        float sinp = 2.f * (w * y - z * x);
        sinp = clamp1(sinp);
        float pitch = asinf(sinp);
        float roll = atan2f(2.f * (w * x + y * z), 1.f - 2.f * (x * x + y * y));
        float yaw = atan2f(2.f * (w * z + x * y), 1.f - 2.f * (y * y + z * z));
        return Vec3(roll, pitch, yaw);
    }

    Quaternion conjugate() const { return Quaternion(-x, -y, -z, w); }
    Quaternion inverted() const
    {
        float len2 = x * x + y * y + z * z + w * w;
        if (len2 <= 0.0f)
            return Quaternion(); // identidade
        float inv = 1.0f / len2;
        return Quaternion(-x * inv, -y * inv, -z * inv, w * inv);
    }

    Quaternion Roll(float x)
    {
        return Quaternion(sinf(x * 0.5f), 0, 0, cosf(x * 0.5f));
    }

    Quaternion Pitch(float x)
    {
        return Quaternion(0, sinf(x * 0.5f), 0, cosf(x * 0.5f));
    }

    Quaternion Yaw(float x)
    {
        return Quaternion(0, 0, sinf(x * 0.5f), cosf(x * 0.5f));
    }

    void set(const float x, const float y, const float z, const float w)
    {
        this->x = x;
        this->y = y;
        this->z = z;
        this->w = w;
    }
    // ----------------
    // Other operations
    // ----------------
    Vec3 rotateVector(const Vec3 &v) const
    {
        // v' = q * (0,v) * conj(q)  (otimizado)
        Vec3 qv(x, y, z);
        Vec3 t = 2.0f * Vec3::Cross(qv, v);
        return v + w * t + Vec3::Cross(qv, t);
    }

    Quaternion slerp(const Quaternion &q, const float t) const
    {
        // Spherical linear interpolation between two quaternions
        // Note: SLERP is not commutative

        Quaternion q1(q);

        // Calculate cosine
        float cosTheta = x * q.x + y * q.y + z * q.z + w * q.w;

        // Use the shortest path
        if (cosTheta < 0)
        {
            cosTheta = -cosTheta;
            q1.x = -q.x;
            q1.y = -q.y;
            q1.z = -q.z;
            q1.w = -q.w;
        }

        // Initialize with linear interpolation
        float scale0 = 1 - t, scale1 = t;

        // Use spherical interpolation only if the quaternions are not very
        // close
        if ((1 - cosTheta) > 0.001f)
        {
            // SLERP
            float theta = acosf(cosTheta);
            float sinTheta = sinf(theta);
            scale0 = sinf((1 - t) * theta) / sinTheta;
            scale1 = sinf(t * theta) / sinTheta;
        }

        // Calculate final quaternion
        return Quaternion(
            x * scale0 + q1.x * scale1, y * scale0 + q1.y * scale1,
            z * scale0 + q1.z * scale1, w * scale0 + q1.w * scale1);
    }

    Quaternion nlerp(const Quaternion &q, const float t) const
    {
        // Normalized linear quaternion interpolation
        // Note: NLERP is faster than SLERP and commutative but does not yield
        // constant velocity

        Quaternion qt;
        float cosTheta = x * q.x + y * q.y + z * q.z + w * q.w;

        // Use the shortest path and interpolate linearly
        if (cosTheta < 0)
            qt = Quaternion(x + (-q.x - x) * t, y + (-q.y - y) * t,
                            z + (-q.z - z) * t, w + (-q.w - w) * t);
        else
            qt = Quaternion(x + (q.x - x) * t, y + (q.y - y) * t,
                            z + (q.z - z) * t, w + (q.w - w) * t);

        // Return normalized quaternion
        float invLen =
            1.0f / sqrtf(qt.x * qt.x + qt.y * qt.y + qt.z * qt.z + qt.w * qt.w);
        return Quaternion(qt.x * invLen, qt.y * invLen, qt.z * invLen,
                          qt.w * invLen);
    }

    static Quaternion Slerp(const Quaternion &a, const Quaternion &b,
                            const float t)
    {
        return a.slerp(b, t);
    }

    static Quaternion Nlerp(const Quaternion &a, const Quaternion &b,
                            const float t)
    {
        return a.nlerp(b, t);
    }

    static Quaternion Normalize(const Quaternion &q)
    {
        Quaternion out(q);
        out.normalize();
        return out;
    }
    static Quaternion LookRotation(const Vec3 &forwardRaw, const Vec3 &upRaw)
    {
        Vec3 f = forwardRaw.normalized();
        Vec3 r = Vec3::Cross(upRaw, f).normalized();
        Vec3 u = Vec3::Cross(f, r);
        // converte matriz 3x3 para quat (m é column-major: col0=r, col1=u,
        // col2=f)
        float m00 = r.x, m01 = u.x, m02 = f.x;
        float m10 = r.y, m11 = u.y, m12 = f.y;
        float m20 = r.z, m21 = u.z, m22 = f.z;
        float t = m00 + m11 + m22;
        Quaternion q;
        if (t > 0.0f)
        {
            float s = sqrtf(t + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (m21 - m12) / s;
            q.y = (m02 - m20) / s;
            q.z = (m10 - m01) / s;
        }
        else if (m00 > m11 && m00 > m22)
        {
            float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
            q.w = (m21 - m12) / s;
            q.x = 0.25f * s;
            q.y = (m01 + m10) / s;
            q.z = (m02 + m20) / s;
        }
        else if (m11 > m22)
        {
            float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
            q.w = (m02 - m20) / s;
            q.x = (m01 + m10) / s;
            q.y = 0.25f * s;
            q.z = (m12 + m21) / s;
        }
        else
        {
            float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
            q.w = (m10 - m01) / s;
            q.x = (m02 + m20) / s;
            q.y = (m12 + m21) / s;
            q.z = 0.25f * s;
        }
        return Normalize(q);
    }
    static Quaternion Identity() { return Quaternion(0, 0, 0, 1); }

    static float Dot(const Quaternion &a, const Quaternion &b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    static Quaternion FromAxisAngle(Vec3 axis, float angle)
    {
        float s = sinf(angle * 0.5f);
        axis.normalize();
        return Quaternion(axis.x * s, axis.y * s, axis.z * s,
                          cosf(angle * 0.5f));
    }

    static Quaternion FromTwoVectors(const Vec3 &fromRaw, const Vec3 &toRaw)
    {
        Vec3 from = fromRaw.normalized();
        Vec3 to = toRaw.normalized();
        float d = Vec3::Dot(from, to);
        if (d > 1.0f - 1e-6f)
            return Identity();
        if (d < -1.0f + 1e-6f)
        {
            // escolhe um eixo ortogonal
            Vec3 axis = fabsf(from.x) < 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
            axis = Vec3::Cross(from, axis).normalized();
            return FromAxisAngle(axis, Pi); // 180°
        }
        Vec3 c = Vec3::Cross(from, to);
        float s = sqrtf((1.0f + d) * 2.0f);
        float invs = 1.0f / s;
        return Normalize(
            Quaternion(c.x * invs, c.y * invs, c.z * invs, s * 0.5f));
    }
};

// -------------------------------------------------------------------------------------------------
// Matrix
// -------------------------------------------------------------------------------------------------

struct Mat4
{

    union
    {
        float c[4][4]; // Column major order for OpenGL: c[column][row]
        float x[16];
    };

    // --------------
    // Static methods
    // --------------
    static Mat4 Translate(float x, float y, float z)
    {
        Mat4 m;

        m.c[3][0] = x;
        m.c[3][1] = y;
        m.c[3][2] = z;

        return m;
    }
    static Mat4 Translate(const Vec3 &v) { return Translate(v.x, v.y, v.z); }

    static Mat4 Scale(float x, float y, float z)
    {
        Mat4 m;

        m.c[0][0] = x;
        m.c[1][1] = y;
        m.c[2][2] = z;

        return m;
    }

    static Mat4 Rotate(float x, float y, float z)
    {
        // Rotation order: YXZ [* Vector]
        return Mat4(Quaternion(x, y, z));
    }

    static Mat4 Rotate(const Quaternion &q) { return Mat4(q); }

    static Mat4 Rotate(Vec3 axis, float angle)
    {
        axis = axis * sinf(angle * 0.5f);
        return Mat4(Quaternion(axis.x, axis.y, axis.z, cosf(angle * 0.5f)));
    }
    static Mat4 Perspective(double fovY, double aspect, double nearPlane,
                            double farPlane)
    {
        double height = 1.0 / tan(fovY * Pi / 360.0);
        double width = height / aspect;
        double f = farPlane;
        double n = nearPlane;

        Mat4 m;

        m.x[0] = width;
        m.x[5] = height;
        m.x[10] = (f + n) / (n - f);
        m.x[11] = -1;
        m.x[14] = 2 * f * n / (n - f);
        m.x[15] = 0;

        return m;
    }
    static Mat4 Perspective(float l, float r, float b, float t, float n,
                            float f)
    {
        Mat4 m;

        m.x[0] = 2 * n / (r - l);
        m.x[5] = 2 * n / (t - b);
        m.x[8] = (r + l) / (r - l);
        m.x[9] = (t + b) / (t - b);
        m.x[10] = -(f + n) / (f - n);
        m.x[11] = -1;
        m.x[14] = -2 * f * n / (f - n);
        m.x[15] = 0;

        return m;
    }

    static Mat4 Ortho(float l, float r, float b, float t, float n, float f)
    {
        Mat4 m;

        m.x[0] = 2 / (r - l);
        m.x[5] = 2 / (t - b);
        m.x[10] = -2 / (f - n);
        m.x[12] = -(r + l) / (r - l);
        m.x[13] = -(t + b) / (t - b);
        m.x[14] = -(f + n) / (f - n);

        return m;
    }

    static Mat4 LookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up)
    {
        Vec3 f = (center - eye).normalized();
        Vec3 u = up.normalized();
        Vec3 s = Vec3::Cross(f, u).normalized();
        u = Vec3::Cross(s, f);

        Mat4 m;

        m.c[0][0] = s.x;
        m.c[1][0] = s.y;
        m.c[2][0] = s.z;

        m.c[0][1] = u.x;
        m.c[1][1] = u.y;
        m.c[2][1] = u.z;

        m.c[0][2] = -f.x;
        m.c[1][2] = -f.y;
        m.c[2][2] = -f.z;

        m.c[3][0] = -s.dot(eye);
        m.c[3][1] = -u.dot(eye);
        m.c[3][2] = f.dot(eye);

        return m;
    }

    static void fastMult43(Mat4 &dst, const Mat4 &m1, const Mat4 &m2)
    {
        // Note: dst may not be the same as m1 or m2

        float *dstx = dst.x;
        const float *m1x = m1.x;
        const float *m2x = m2.x;

        dstx[0] = m1x[0] * m2x[0] + m1x[4] * m2x[1] + m1x[8] * m2x[2];
        dstx[1] = m1x[1] * m2x[0] + m1x[5] * m2x[1] + m1x[9] * m2x[2];
        dstx[2] = m1x[2] * m2x[0] + m1x[6] * m2x[1] + m1x[10] * m2x[2];
        dstx[3] = 0.0f;

        dstx[4] = m1x[0] * m2x[4] + m1x[4] * m2x[5] + m1x[8] * m2x[6];
        dstx[5] = m1x[1] * m2x[4] + m1x[5] * m2x[5] + m1x[9] * m2x[6];
        dstx[6] = m1x[2] * m2x[4] + m1x[6] * m2x[5] + m1x[10] * m2x[6];
        dstx[7] = 0.0f;

        dstx[8] = m1x[0] * m2x[8] + m1x[4] * m2x[9] + m1x[8] * m2x[10];
        dstx[9] = m1x[1] * m2x[8] + m1x[5] * m2x[9] + m1x[9] * m2x[10];
        dstx[10] = m1x[2] * m2x[8] + m1x[6] * m2x[9] + m1x[10] * m2x[10];
        dstx[11] = 0.0f;

        dstx[12] = m1x[0] * m2x[12] + m1x[4] * m2x[13] + m1x[8] * m2x[14] + m1x[12] * m2x[15];
        dstx[13] = m1x[1] * m2x[12] + m1x[5] * m2x[13] + m1x[9] * m2x[14] + m1x[13] * m2x[15];
        dstx[14] = m1x[2] * m2x[12] + m1x[6] * m2x[13] + m1x[10] * m2x[14] + m1x[14] * m2x[15];
        dstx[15] = 1.0f;
    }

    static Mat4 Identity()
    {
        Mat4 m(NO_INIT);
        m.x[0] = 1;
        m.x[1] = 0;
        m.x[2] = 0;
        m.x[3] = 0;
        m.x[4] = 0;
        m.x[5] = 1;
        m.x[6] = 0;
        m.x[7] = 0;
        m.x[8] = 0;
        m.x[9] = 0;
        m.x[10] = 1;
        m.x[11] = 0;
        m.x[12] = 0;
        m.x[13] = 0;
        m.x[14] = 0;
        m.x[15] = 1;
        return m;
    }

    void set(const float *floatArray16)
    {
        x[0] = floatArray16[0];
        x[1] = floatArray16[1];
        x[2] = floatArray16[2];
        x[3] = floatArray16[3];
        x[4] = floatArray16[4];
        x[5] = floatArray16[5];
        x[6] = floatArray16[6];
        x[7] = floatArray16[7];
        x[8] = floatArray16[8];
        x[9] = floatArray16[9];
        x[10] = floatArray16[10];
        x[11] = floatArray16[11];
        x[12] = floatArray16[12];
        x[13] = floatArray16[13];
        x[14] = floatArray16[14];
        x[15] = floatArray16[15];
    }

    // ------------
    // Constructors
    // ------------
    Mat4()
    {
        c[0][0] = 1;
        c[1][0] = 0;
        c[2][0] = 0;
        c[3][0] = 0;
        c[0][1] = 0;
        c[1][1] = 1;
        c[2][1] = 0;
        c[3][1] = 0;
        c[0][2] = 0;
        c[1][2] = 0;
        c[2][2] = 1;
        c[3][2] = 0;
        c[0][3] = 0;
        c[1][3] = 0;
        c[2][3] = 0;
        c[3][3] = 1;
    }

    explicit Mat4(NoInitHint)
    {
        // Constructor without default initialization
    }

    Mat4(const float *floatArray16)
    {
        for (unsigned int i = 0; i < 4; ++i)
        {
            for (unsigned int j = 0; j < 4; ++j)
            {
                c[i][j] = floatArray16[i * 4 + j];
            }
        }
    }

    Mat4(const Quaternion &q)
    {
        // Calculate coefficients
        float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
        float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
        float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
        float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;

        c[0][0] = 1 - (yy + zz);
        c[1][0] = xy - wz;
        c[2][0] = xz + wy;
        c[3][0] = 0;
        c[0][1] = xy + wz;
        c[1][1] = 1 - (xx + zz);
        c[2][1] = yz - wx;
        c[3][1] = 0;
        c[0][2] = xz - wy;
        c[1][2] = yz + wx;
        c[2][2] = 1 - (xx + yy);
        c[3][2] = 0;
        c[0][3] = 0;
        c[1][3] = 0;
        c[2][3] = 0;
        c[3][3] = 1;
    }

    // ----------
    // Matrix sum
    // ----------
    Mat4 operator+(const Mat4 &m) const
    {
        Mat4 mf(NO_INIT);

        mf.x[0] = x[0] + m.x[0];
        mf.x[1] = x[1] + m.x[1];
        mf.x[2] = x[2] + m.x[2];
        mf.x[3] = x[3] + m.x[3];
        mf.x[4] = x[4] + m.x[4];
        mf.x[5] = x[5] + m.x[5];
        mf.x[6] = x[6] + m.x[6];
        mf.x[7] = x[7] + m.x[7];
        mf.x[8] = x[8] + m.x[8];
        mf.x[9] = x[9] + m.x[9];
        mf.x[10] = x[10] + m.x[10];
        mf.x[11] = x[11] + m.x[11];
        mf.x[12] = x[12] + m.x[12];
        mf.x[13] = x[13] + m.x[13];
        mf.x[14] = x[14] + m.x[14];
        mf.x[15] = x[15] + m.x[15];

        return mf;
    }

    Mat4 &operator+=(const Mat4 &m) { return *this = *this + m; }

    // ---------------------
    // Matrix multiplication
    // ---------------------
    Mat4 operator*(const Mat4 &m) const
    {
        Mat4 mf(NO_INIT);

        mf.x[0] =
            x[0] * m.x[0] + x[4] * m.x[1] + x[8] * m.x[2] + x[12] * m.x[3];
        mf.x[1] =
            x[1] * m.x[0] + x[5] * m.x[1] + x[9] * m.x[2] + x[13] * m.x[3];
        mf.x[2] =
            x[2] * m.x[0] + x[6] * m.x[1] + x[10] * m.x[2] + x[14] * m.x[3];
        mf.x[3] =
            x[3] * m.x[0] + x[7] * m.x[1] + x[11] * m.x[2] + x[15] * m.x[3];

        mf.x[4] =
            x[0] * m.x[4] + x[4] * m.x[5] + x[8] * m.x[6] + x[12] * m.x[7];
        mf.x[5] =
            x[1] * m.x[4] + x[5] * m.x[5] + x[9] * m.x[6] + x[13] * m.x[7];
        mf.x[6] =
            x[2] * m.x[4] + x[6] * m.x[5] + x[10] * m.x[6] + x[14] * m.x[7];
        mf.x[7] =
            x[3] * m.x[4] + x[7] * m.x[5] + x[11] * m.x[6] + x[15] * m.x[7];

        mf.x[8] =
            x[0] * m.x[8] + x[4] * m.x[9] + x[8] * m.x[10] + x[12] * m.x[11];
        mf.x[9] =
            x[1] * m.x[8] + x[5] * m.x[9] + x[9] * m.x[10] + x[13] * m.x[11];
        mf.x[10] =
            x[2] * m.x[8] + x[6] * m.x[9] + x[10] * m.x[10] + x[14] * m.x[11];
        mf.x[11] =
            x[3] * m.x[8] + x[7] * m.x[9] + x[11] * m.x[10] + x[15] * m.x[11];

        mf.x[12] =
            x[0] * m.x[12] + x[4] * m.x[13] + x[8] * m.x[14] + x[12] * m.x[15];
        mf.x[13] =
            x[1] * m.x[12] + x[5] * m.x[13] + x[9] * m.x[14] + x[13] * m.x[15];
        mf.x[14] =
            x[2] * m.x[12] + x[6] * m.x[13] + x[10] * m.x[14] + x[14] * m.x[15];
        mf.x[15] =
            x[3] * m.x[12] + x[7] * m.x[13] + x[11] * m.x[14] + x[15] * m.x[15];

        return mf;
    }

    Mat4 operator*(const float f) const
    {
        Mat4 m(*this);

        m.x[0] *= f;
        m.x[1] *= f;
        m.x[2] *= f;
        m.x[3] *= f;
        m.x[4] *= f;
        m.x[5] *= f;
        m.x[6] *= f;
        m.x[7] *= f;
        m.x[8] *= f;
        m.x[9] *= f;
        m.x[10] *= f;
        m.x[11] *= f;
        m.x[12] *= f;
        m.x[13] *= f;
        m.x[14] *= f;
        m.x[15] *= f;

        return m;
    }

    // ----------------------------
    // Vector-Matrix multiplication
    // ----------------------------
    Vec3 operator*(const Vec3 &v) const
    {
        return Vec3(v.x * c[0][0] + v.y * c[1][0] + v.z * c[2][0] + c[3][0],
                    v.x * c[0][1] + v.y * c[1][1] + v.z * c[2][1] + c[3][1],
                    v.x * c[0][2] + v.y * c[1][2] + v.z * c[2][2] + c[3][2]);
    }

    Vec4 operator*(const Vec4 &v) const
    {
        return Vec4(
            v.x * c[0][0] + v.y * c[1][0] + v.z * c[2][0] + v.w * c[3][0],
            v.x * c[0][1] + v.y * c[1][1] + v.z * c[2][1] + v.w * c[3][1],
            v.x * c[0][2] + v.y * c[1][2] + v.z * c[2][2] + v.w * c[3][2],
            v.x * c[0][3] + v.y * c[1][3] + v.z * c[2][3] + v.w * c[3][3]);
    }

    Vec3 mult33Vec(const Vec3 &v) const
    {
        return Vec3(v.x * c[0][0] + v.y * c[1][0] + v.z * c[2][0],
                    v.x * c[0][1] + v.y * c[1][1] + v.z * c[2][1],
                    v.x * c[0][2] + v.y * c[1][2] + v.z * c[2][2]);
    }

    // ---------------
    // Transformations
    // ---------------
    void translate(const float tx, const float ty, const float tz)
    {
        *this = Translate(tx, ty, tz) * *this;
    }

    void scale(const float sx, const float sy, const float sz)
    {
        *this = Scale(sx, sy, sz) * *this;
    }

    void rotate(const float rx, const float ry, const float rz)
    {
        *this = Rotate(rx, ry, rz) * *this;
    }

    void rotate(const float angle, const Vec3 &axis)
    {
        *this = Rotate(axis, angle) * *this;
    }

    void rotate(const Quaternion &q) { *this = Rotate(q) * *this; }

    // ---------------
    // Other
    // ---------------

    Mat4 transposed() const
    {
        Mat4 m(*this);

        for (unsigned int maty = 0; maty < 4; ++maty)
        {
            for (unsigned int matx = maty + 1; matx < 4; ++matx)
            {
                float tmp = m.c[matx][maty];
                m.c[matx][maty] = m.c[maty][matx];
                m.c[maty][matx] = tmp;
            }
        }

        return m;
    }

    float determinant() const
    {
        return c[0][3] * c[1][2] * c[2][1] * c[3][0] - c[0][2] * c[1][3] * c[2][1] * c[3][0] - c[0][3] * c[1][1] * c[2][2] * c[3][0] + c[0][1] * c[1][3] * c[2][2] * c[3][0] + c[0][2] * c[1][1] * c[2][3] * c[3][0] - c[0][1] * c[1][2] * c[2][3] * c[3][0] - c[0][3] * c[1][2] * c[2][0] * c[3][1] + c[0][2] * c[1][3] * c[2][0] * c[3][1] + c[0][3] * c[1][0] * c[2][2] * c[3][1] - c[0][0] * c[1][3] * c[2][2] * c[3][1] - c[0][2] * c[1][0] * c[2][3] * c[3][1] + c[0][0] * c[1][2] * c[2][3] * c[3][1] + c[0][3] * c[1][1] * c[2][0] * c[3][2] - c[0][1] * c[1][3] * c[2][0] * c[3][2] - c[0][3] * c[1][0] * c[2][1] * c[3][2] + c[0][0] * c[1][3] * c[2][1] * c[3][2] + c[0][1] * c[1][0] * c[2][3] * c[3][2] - c[0][0] * c[1][1] * c[2][3] * c[3][2] - c[0][2] * c[1][1] * c[2][0] * c[3][3] + c[0][1] * c[1][2] * c[2][0] * c[3][3] + c[0][2] * c[1][0] * c[2][1] * c[3][3] - c[0][0] * c[1][2] * c[2][1] * c[3][3] - c[0][1] * c[1][0] * c[2][2] * c[3][3] + c[0][0] * c[1][1] * c[2][2] * c[3][3];
    }

    Mat4 inverted() const
    {
        Mat4 m(NO_INIT);

        float d = determinant();
        if (d == 0)
            return m;
        d = 1.0f / d;

        m.c[0][0] = d * (c[1][2] * c[2][3] * c[3][1] - c[1][3] * c[2][2] * c[3][1] + c[1][3] * c[2][1] * c[3][2] - c[1][1] * c[2][3] * c[3][2] - c[1][2] * c[2][1] * c[3][3] + c[1][1] * c[2][2] * c[3][3]);
        m.c[0][1] = d * (c[0][3] * c[2][2] * c[3][1] - c[0][2] * c[2][3] * c[3][1] - c[0][3] * c[2][1] * c[3][2] + c[0][1] * c[2][3] * c[3][2] + c[0][2] * c[2][1] * c[3][3] - c[0][1] * c[2][2] * c[3][3]);
        m.c[0][2] = d * (c[0][2] * c[1][3] * c[3][1] - c[0][3] * c[1][2] * c[3][1] + c[0][3] * c[1][1] * c[3][2] - c[0][1] * c[1][3] * c[3][2] - c[0][2] * c[1][1] * c[3][3] + c[0][1] * c[1][2] * c[3][3]);
        m.c[0][3] = d * (c[0][3] * c[1][2] * c[2][1] - c[0][2] * c[1][3] * c[2][1] - c[0][3] * c[1][1] * c[2][2] + c[0][1] * c[1][3] * c[2][2] + c[0][2] * c[1][1] * c[2][3] - c[0][1] * c[1][2] * c[2][3]);
        m.c[1][0] = d * (c[1][3] * c[2][2] * c[3][0] - c[1][2] * c[2][3] * c[3][0] - c[1][3] * c[2][0] * c[3][2] + c[1][0] * c[2][3] * c[3][2] + c[1][2] * c[2][0] * c[3][3] - c[1][0] * c[2][2] * c[3][3]);
        m.c[1][1] = d * (c[0][2] * c[2][3] * c[3][0] - c[0][3] * c[2][2] * c[3][0] + c[0][3] * c[2][0] * c[3][2] - c[0][0] * c[2][3] * c[3][2] - c[0][2] * c[2][0] * c[3][3] + c[0][0] * c[2][2] * c[3][3]);
        m.c[1][2] = d * (c[0][3] * c[1][2] * c[3][0] - c[0][2] * c[1][3] * c[3][0] - c[0][3] * c[1][0] * c[3][2] + c[0][0] * c[1][3] * c[3][2] + c[0][2] * c[1][0] * c[3][3] - c[0][0] * c[1][2] * c[3][3]);
        m.c[1][3] = d * (c[0][2] * c[1][3] * c[2][0] - c[0][3] * c[1][2] * c[2][0] + c[0][3] * c[1][0] * c[2][2] - c[0][0] * c[1][3] * c[2][2] - c[0][2] * c[1][0] * c[2][3] + c[0][0] * c[1][2] * c[2][3]);
        m.c[2][0] = d * (c[1][1] * c[2][3] * c[3][0] - c[1][3] * c[2][1] * c[3][0] + c[1][3] * c[2][0] * c[3][1] - c[1][0] * c[2][3] * c[3][1] - c[1][1] * c[2][0] * c[3][3] + c[1][0] * c[2][1] * c[3][3]);
        m.c[2][1] = d * (c[0][3] * c[2][1] * c[3][0] - c[0][1] * c[2][3] * c[3][0] - c[0][3] * c[2][0] * c[3][1] + c[0][0] * c[2][3] * c[3][1] + c[0][1] * c[2][0] * c[3][3] - c[0][0] * c[2][1] * c[3][3]);
        m.c[2][2] = d * (c[0][1] * c[1][3] * c[3][0] - c[0][3] * c[1][1] * c[3][0] + c[0][3] * c[1][0] * c[3][1] - c[0][0] * c[1][3] * c[3][1] - c[0][1] * c[1][0] * c[3][3] + c[0][0] * c[1][1] * c[3][3]);
        m.c[2][3] = d * (c[0][3] * c[1][1] * c[2][0] - c[0][1] * c[1][3] * c[2][0] - c[0][3] * c[1][0] * c[2][1] + c[0][0] * c[1][3] * c[2][1] + c[0][1] * c[1][0] * c[2][3] - c[0][0] * c[1][1] * c[2][3]);
        m.c[3][0] = d * (c[1][2] * c[2][1] * c[3][0] - c[1][1] * c[2][2] * c[3][0] - c[1][2] * c[2][0] * c[3][1] + c[1][0] * c[2][2] * c[3][1] + c[1][1] * c[2][0] * c[3][2] - c[1][0] * c[2][1] * c[3][2]);
        m.c[3][1] = d * (c[0][1] * c[2][2] * c[3][0] - c[0][2] * c[2][1] * c[3][0] + c[0][2] * c[2][0] * c[3][1] - c[0][0] * c[2][2] * c[3][1] - c[0][1] * c[2][0] * c[3][2] + c[0][0] * c[2][1] * c[3][2]);
        m.c[3][2] = d * (c[0][2] * c[1][1] * c[3][0] - c[0][1] * c[1][2] * c[3][0] - c[0][2] * c[1][0] * c[3][1] + c[0][0] * c[1][2] * c[3][1] + c[0][1] * c[1][0] * c[3][2] - c[0][0] * c[1][1] * c[3][2]);
        m.c[3][3] = d * (c[0][1] * c[1][2] * c[2][0] - c[0][2] * c[1][1] * c[2][0] + c[0][2] * c[1][0] * c[2][1] - c[0][0] * c[1][2] * c[2][1] - c[0][1] * c[1][0] * c[2][2] + c[0][0] * c[1][1] * c[2][2]);

        return m;
    }

    void decompose(Vec3 &trans, Vec3 &rot, Vec3 &scale) const
    {
        // Getting translation is trivial
        trans = Vec3(c[3][0], c[3][1], c[3][2]);

        // Scale is length of columns
        scale.x =
            sqrtf(c[0][0] * c[0][0] + c[0][1] * c[0][1] + c[0][2] * c[0][2]);
        scale.y =
            sqrtf(c[1][0] * c[1][0] + c[1][1] * c[1][1] + c[1][2] * c[1][2]);
        scale.z =
            sqrtf(c[2][0] * c[2][0] + c[2][1] * c[2][1] + c[2][2] * c[2][2]);

        if (scale.x == 0 || scale.y == 0 || scale.z == 0)
            return;

        // Detect negative scale with determinant and flip one arbitrary axis
        if (determinant() < 0)
            scale.x = -scale.x;

        // Combined rotation matrix YXZ
        //
        // Cos[y]*Cos[z]+Sin[x]*Sin[y]*Sin[z] Cos[z]*Sin[x]*Sin[y]-Cos[y]*Sin[z]
        // Cos[x]*Sin[y] Cos[x]*Sin[z]                        Cos[x]*Cos[z]
        // -Sin[x] -Cos[z]*Sin[y]+Cos[y]*Sin[x]*Sin[z]
        // Cos[y]*Cos[z]*Sin[x]+Sin[y]*Sin[z]  Cos[x]*Cos[y]

        rot.x = asinf(-c[2][1] / scale.z);

        // Special case: Cos[x] == 0 (when Sin[x] is +/-1)
        float f = fabsf(c[2][1] / scale.z);
        if (f > 0.999f && f < 1.001f)
        {
            // Pin arbitrarily one of y or z to zero
            // Mathematical equivalent of gimbal lock
            rot.y = 0;

            // Now: Cos[x] = 0, Sin[x] = +/-1, Cos[y] = 1, Sin[y] = 0
            // => m[0][0] = Cos[z] and m[1][0] = Sin[z]
            rot.z = atan2f(-c[1][0] / scale.y, c[0][0] / scale.x);
        }
        // Standard case
        else
        {
            rot.y = atan2f(c[2][0] / scale.z, c[2][2] / scale.z);
            rot.z = atan2f(c[0][1] / scale.x, c[1][1] / scale.y);
        }
    }

    void setCol(unsigned int col, const Vec4 &v)
    {
        x[col * 4 + 0] = v.x;
        x[col * 4 + 1] = v.y;
        x[col * 4 + 2] = v.z;
        x[col * 4 + 3] = v.w;
    }

    Vec4 getCol(unsigned int col) const
    {
        return Vec4(x[col * 4 + 0], x[col * 4 + 1], x[col * 4 + 2],
                    x[col * 4 + 3]);
    }

    Vec4 getRow(unsigned int row) const
    {
        return Vec4(x[row + 0], x[row + 4], x[row + 8], x[row + 12]);
    }

    Vec3 getTrans() const { return Vec3(c[3][0], c[3][1], c[3][2]); }

    Vec3 getScale() const
    {
        Vec3 scale;
        // Scale is length of columns
        scale.x =
            sqrtf(c[0][0] * c[0][0] + c[0][1] * c[0][1] + c[0][2] * c[0][2]);
        scale.y =
            sqrtf(c[1][0] * c[1][0] + c[1][1] * c[1][1] + c[1][2] * c[1][2]);
        scale.z =
            sqrtf(c[2][0] * c[2][0] + c[2][1] * c[2][1] + c[2][2] * c[2][2]);
        return scale;
    }
    void identity()
    {
        c[0][0] = 1;
        c[1][0] = 0;
        c[2][0] = 0;
        c[3][0] = 0;
        c[0][1] = 0;
        c[1][1] = 1;
        c[2][1] = 0;
        c[3][1] = 0;
        c[0][2] = 0;
        c[1][2] = 0;
        c[2][2] = 1;
        c[3][2] = 0;
        c[0][3] = 0;
        c[1][3] = 0;
        c[2][3] = 0;
        c[3][3] = 1;
    }
    static Vec3 Transform(const Mat4 &m, const Vec3 &v)
    {
        return Vec3(
            v.x * m.c[0][0] + v.y * m.c[1][0] + v.z * m.c[2][0] + m.c[3][0],
            v.x * m.c[0][1] + v.y * m.c[1][1] + v.z * m.c[2][1] + m.c[3][1],
            v.x * m.c[0][2] + v.y * m.c[1][2] + v.z * m.c[2][2] + m.c[3][2]);
    }

    static void Transform(const Mat4 &m, const Vec3 &v, Vec3 &out)
    {
        out.x = v.x * m.c[0][0] + v.y * m.c[1][0] + v.z * m.c[2][0] + m.c[3][0];
        out.y = v.x * m.c[0][1] + v.y * m.c[1][1] + v.z * m.c[2][1] + m.c[3][1];
        out.z = v.x * m.c[0][2] + v.y * m.c[1][2] + v.z * m.c[2][2] + m.c[3][2];
    }

    static Vec3 TransformNormal(const Mat4 &m, const Vec3 &v)
    {
        return Vec3(v.x * m.c[0][0] + v.y * m.c[1][0] + v.z * m.c[2][0],
                    v.x * m.c[0][1] + v.y * m.c[1][1] + v.z * m.c[2][1],
                    v.x * m.c[0][2] + v.y * m.c[1][2] + v.z * m.c[2][2]);
    }
    static Mat4 NormalMatrix(const Mat4 &model)
    {
        // inverse-transpose do bloco 3x3, devolvido em 4x4 com última
        // linha/coluna [0,0,0,1]
        Mat4 inv = Inverse(model);
        Mat4 n = inv.transposed();
        n.c[0][3] = n.c[1][3] = n.c[2][3] = 0;
        n.c[3][0] = n.c[3][1] = n.c[3][2] = 0;
        n.c[3][3] = 1;
        return n;
    }

    static Mat4 Inverse(const Mat4 &mat)
    {
        Mat4 m(NO_INIT);
        float d = mat.determinant();
        if (d == 0)
            return m;
        d = 1.0f / d;

        m.c[0][0] = d * (mat.c[1][2] * mat.c[2][3] * mat.c[3][1] - mat.c[1][3] * mat.c[2][2] * mat.c[3][1] + mat.c[1][3] * mat.c[2][1] * mat.c[3][2] - mat.c[1][1] * mat.c[2][3] * mat.c[3][2] - mat.c[1][2] * mat.c[2][1] * mat.c[3][3] + mat.c[1][1] * mat.c[2][2] * mat.c[3][3]);
        m.c[0][1] = d * (mat.c[0][3] * mat.c[2][2] * mat.c[3][1] - mat.c[0][2] * mat.c[2][3] * mat.c[3][1] - mat.c[0][3] * mat.c[2][1] * mat.c[3][2] + mat.c[0][1] * mat.c[2][3] * mat.c[3][2] + mat.c[0][2] * mat.c[2][1] * mat.c[3][3] - mat.c[0][1] * mat.c[2][2] * mat.c[3][3]);
        m.c[0][2] = d * (mat.c[0][2] * mat.c[1][3] * mat.c[3][1] - mat.c[0][3] * mat.c[1][2] * mat.c[3][1] + mat.c[0][3] * mat.c[1][1] * mat.c[3][2] - mat.c[0][1] * mat.c[1][3] * mat.c[3][2] - mat.c[0][2] * mat.c[1][1] * mat.c[3][3] + mat.c[0][1] * mat.c[1][2] * mat.c[3][3]);
        m.c[0][3] = d * (mat.c[0][3] * mat.c[1][2] * mat.c[2][1] - mat.c[0][2] * mat.c[1][3] * mat.c[2][1] - mat.c[0][3] * mat.c[1][1] * mat.c[2][2] + mat.c[0][1] * mat.c[1][3] * mat.c[2][2] + mat.c[0][2] * mat.c[1][1] * mat.c[2][3] - mat.c[0][1] * mat.c[1][2] * mat.c[2][3]);
        m.c[1][0] = d * (mat.c[1][3] * mat.c[2][2] * mat.c[3][0] - mat.c[1][2] * mat.c[2][3] * mat.c[3][0] - mat.c[1][3] * mat.c[2][0] * mat.c[3][2] + mat.c[1][0] * mat.c[2][3] * mat.c[3][2] + mat.c[1][2] * mat.c[2][0] * mat.c[3][3] - mat.c[1][0] * mat.c[2][2] * mat.c[3][3]);
        m.c[1][1] = d * (mat.c[0][2] * mat.c[2][3] * mat.c[3][0] - mat.c[0][3] * mat.c[2][2] * mat.c[3][0] + mat.c[0][3] * mat.c[2][0] * mat.c[3][2] - mat.c[0][0] * mat.c[2][3] * mat.c[3][2] - mat.c[0][2] * mat.c[2][0] * mat.c[3][3] + mat.c[0][0] * mat.c[2][2] * mat.c[3][3]);
        m.c[1][2] = d * (mat.c[0][3] * mat.c[1][2] * mat.c[3][0] - mat.c[0][2] * mat.c[1][3] * mat.c[3][0] - mat.c[0][3] * mat.c[1][0] * mat.c[3][2] + mat.c[0][0] * mat.c[1][3] * mat.c[3][2] + mat.c[0][2] * mat.c[1][0] * mat.c[3][3] - mat.c[0][0] * mat.c[1][2] * mat.c[3][3]);
        m.c[1][3] = d * (mat.c[0][2] * mat.c[1][3] * mat.c[2][0] - mat.c[0][3] * mat.c[1][2] * mat.c[2][0] + mat.c[0][3] * mat.c[1][0] * mat.c[2][2] - mat.c[0][0] * mat.c[1][3] * mat.c[2][2] - mat.c[0][2] * mat.c[1][0] * mat.c[2][3] + mat.c[0][0] * mat.c[1][2] * mat.c[2][3]);
        m.c[2][0] = d * (mat.c[1][1] * mat.c[2][3] * mat.c[3][0] - mat.c[1][3] * mat.c[2][1] * mat.c[3][0] + mat.c[1][3] * mat.c[2][0] * mat.c[3][1] - mat.c[1][0] * mat.c[2][3] * mat.c[3][1] - mat.c[1][1] * mat.c[2][0] * mat.c[3][3] + mat.c[1][0] * mat.c[2][1] * mat.c[3][3]);
        m.c[2][1] = d * (mat.c[0][3] * mat.c[2][1] * mat.c[3][0] - mat.c[0][1] * mat.c[2][3] * mat.c[3][0] - mat.c[0][3] * mat.c[2][0] * mat.c[3][1] + mat.c[0][0] * mat.c[2][3] * mat.c[3][1] + mat.c[0][1] * mat.c[2][0] * mat.c[3][3] - mat.c[0][0] * mat.c[2][1] * mat.c[3][3]);
        m.c[2][2] = d * (mat.c[0][1] * mat.c[1][3] * mat.c[3][0] - mat.c[0][3] * mat.c[1][1] * mat.c[3][0] + mat.c[0][3] * mat.c[1][0] * mat.c[3][1] - mat.c[0][0] * mat.c[1][3] * mat.c[3][1] - mat.c[0][1] * mat.c[1][0] * mat.c[3][3] + mat.c[0][0] * mat.c[1][1] * mat.c[3][3]);
        m.c[2][3] = d * (mat.c[0][3] * mat.c[1][1] * mat.c[2][0] - mat.c[0][1] * mat.c[1][3] * mat.c[2][0] - mat.c[0][3] * mat.c[1][0] * mat.c[2][1] + mat.c[0][0] * mat.c[1][3] * mat.c[2][1] + mat.c[0][1] * mat.c[1][0] * mat.c[2][3] - mat.c[0][0] * mat.c[1][1] * mat.c[2][3]);
        m.c[3][0] = d * (mat.c[1][2] * mat.c[2][1] * mat.c[3][0] - mat.c[1][1] * mat.c[2][2] * mat.c[3][0] - mat.c[1][2] * mat.c[2][0] * mat.c[3][1] + mat.c[1][0] * mat.c[2][2] * mat.c[3][1] + mat.c[1][1] * mat.c[2][0] * mat.c[3][2] - mat.c[1][0] * mat.c[2][1] * mat.c[3][2]);
        m.c[3][1] = d * (mat.c[0][1] * mat.c[2][2] * mat.c[3][0] - mat.c[0][2] * mat.c[2][1] * mat.c[3][0] + mat.c[0][2] * mat.c[2][0] * mat.c[3][1] - mat.c[0][0] * mat.c[2][2] * mat.c[3][1] - mat.c[0][1] * mat.c[2][0] * mat.c[3][2] + mat.c[0][0] * mat.c[2][1] * mat.c[3][2]);
        m.c[3][2] = d * (mat.c[0][2] * mat.c[1][1] * mat.c[3][0] - mat.c[0][1] * mat.c[1][2] * mat.c[3][0] - mat.c[0][2] * mat.c[1][0] * mat.c[3][1] + mat.c[0][0] * mat.c[1][2] * mat.c[3][1] + mat.c[0][1] * mat.c[1][0] * mat.c[3][2] - mat.c[0][0] * mat.c[1][1] * mat.c[3][2]);
        m.c[3][3] = d * (mat.c[0][1] * mat.c[1][2] * mat.c[2][0] - mat.c[0][2] * mat.c[1][1] * mat.c[2][0] + mat.c[0][2] * mat.c[1][0] * mat.c[2][1] - mat.c[0][0] * mat.c[1][2] * mat.c[2][1] - mat.c[0][1] * mat.c[1][0] * mat.c[2][2] + mat.c[0][0] * mat.c[1][1] * mat.c[2][2]);

        return m;
    }
};

// -------------------------------------------------------------------------------------------------
// Mat3
// -------------------------------------------------------------------------------------------------

struct Mat3
{
    union
    {
        float c[3][3]; // column-major: c[col][row]
        float x[9];
    };

    // --- Static builders ---
    static Mat3 Identity()
    {
        Mat3 m(NO_INIT);
        m.x[0] = 1;
        m.x[1] = 0;
        m.x[2] = 0;
        m.x[3] = 0;
        m.x[4] = 1;
        m.x[5] = 0;
        m.x[6] = 0;
        m.x[7] = 0;
        m.x[8] = 1;
        return m;
    }

    static Mat3 Scale(float sx, float sy, float sz)
    {
        Mat3 m = Identity();
        m.c[0][0] = sx;
        m.c[1][1] = sy;
        m.c[2][2] = sz;
        return m;
    }

    // Rodrigues (eixo unitário)
    static Mat3 Rotate(const Vec3 &axisRaw, float angle)
    {
        Vec3 axis = axisRaw.normalized();
        const float s = sinf(angle), c = cosf(angle), t = 1.0f - c;
        const float x = axis.x, y = axis.y, z = axis.z;

        Mat3 m(NO_INIT);
        // linha 0
        m.c[0][0] = t * x * x + c;
        m.c[1][0] = t * x * y + s * z;
        m.c[2][0] = t * x * z - s * y;
        // linha 1
        m.c[0][1] = t * y * x - s * z;
        m.c[1][1] = t * y * y + c;
        m.c[2][1] = t * y * z + s * x;
        // linha 2
        m.c[0][2] = t * z * x + s * y;
        m.c[1][2] = t * z * y - s * x;
        m.c[2][2] = t * z * z + c;
        return m;
    }

    // extrai a 3x3 de um Mat4 (top-left)
    static Mat3 FromMat4(const Mat4 &M)
    {
        Mat3 m(NO_INIT);
        m.c[0][0] = M.c[0][0];
        m.c[1][0] = M.c[1][0];
        m.c[2][0] = M.c[2][0];
        m.c[0][1] = M.c[0][1];
        m.c[1][1] = M.c[1][1];
        m.c[2][1] = M.c[2][1];
        m.c[0][2] = M.c[0][2];
        m.c[1][2] = M.c[1][2];
        m.c[2][2] = M.c[2][2];
        return m;
    }

    // normal matrix (inverse-transpose 3x3)
    static Mat3 NormalMatrix(const Mat4 &model)
    {
        Mat3 A = FromMat4(model);
        return A.inverted().transposed();
    }

    static Mat3 FromColumns(const Vec3 &c0, const Vec3 &c1, const Vec3 &c2)
    {
        Mat3 m(NO_INIT);
        m.c[0][0] = c0.x;
        m.c[0][1] = c0.y;
        m.c[0][2] = c0.z;
        m.c[1][0] = c1.x;
        m.c[1][1] = c1.y;
        m.c[1][2] = c1.z;
        m.c[2][0] = c2.x;
        m.c[2][1] = c2.y;
        m.c[2][2] = c2.z;
        return m;
    }

    static Mat3 FromRows(const Vec3 &r0, const Vec3 &r1, const Vec3 &r2)
    {
        Mat3 m(NO_INIT);
        m.c[0][0] = r0.x;
        m.c[1][0] = r0.y;
        m.c[2][0] = r0.z;
        m.c[0][1] = r1.x;
        m.c[1][1] = r1.y;
        m.c[2][1] = r1.z;
        m.c[0][2] = r2.x;
        m.c[1][2] = r2.y;
        m.c[2][2] = r2.z;
        return m;
    }

    // --- Ctors ---
    Mat3() { *this = Identity(); }
    explicit Mat3(NoInitHint) {} // sem init
    explicit Mat3(const float *a9)
    { // column-major
        for (int i = 0; i < 9; ++i)
            x[i] = a9[i];
    }

    // --- Basic ops ---
    Mat3 transposed() const
    {
        Mat3 m(*this);
        // swap c[i][j] <-> c[j][i]
        float t;
        t = m.c[1][0];
        m.c[1][0] = m.c[0][1];
        m.c[0][1] = t;
        t = m.c[2][0];
        m.c[2][0] = m.c[0][2];
        m.c[0][2] = t;
        t = m.c[2][1];
        m.c[2][1] = m.c[1][2];
        m.c[1][2] = t;
        return m;
    }

    float determinant() const
    {
        // ler em "row-major lógico"
        const float a00 = c[0][0], a01 = c[1][0], a02 = c[2][0];
        const float a10 = c[0][1], a11 = c[1][1], a12 = c[2][1];
        const float a20 = c[0][2], a21 = c[1][2], a22 = c[2][2];
        return a00 * (a11 * a22 - a21 * a12) - a01 * (a10 * a22 - a20 * a12) + a02 * (a10 * a21 - a20 * a11);
    }

    Mat3 inverted() const
    {
        Mat3 r(NO_INIT);
        const float a00 = c[0][0], a01 = c[1][0], a02 = c[2][0];
        const float a10 = c[0][1], a11 = c[1][1], a12 = c[2][1];
        const float a20 = c[0][2], a21 = c[1][2], a22 = c[2][2];

        const float b00 = a11 * a22 - a21 * a12;
        const float b01 = a02 * a21 - a01 * a22;
        const float b02 = a01 * a12 - a02 * a11;
        const float b10 = a12 * a20 - a10 * a22;
        const float b11 = a00 * a22 - a02 * a20;
        const float b12 = a02 * a10 - a00 * a12;
        const float b20 = a10 * a21 - a20 * a11;
        const float b21 = a01 * a20 - a00 * a21;
        const float b22 = a00 * a11 - a01 * a10;

        const float det = a00 * b00 + a01 * b10 + a02 * b20;
        if (fabsf(det) < 1e-20f)
            return r; // devolve lixo não inicializado ( Mat4::inverted)

        const float invDet = 1.0f / det;

        // adjugate transposta em column-major
        r.c[0][0] = b00 * invDet;
        r.c[1][0] = b10 * invDet;
        r.c[2][0] = b20 * invDet;
        r.c[0][1] = b01 * invDet;
        r.c[1][1] = b11 * invDet;
        r.c[2][1] = b21 * invDet;
        r.c[0][2] = b02 * invDet;
        r.c[1][2] = b12 * invDet;
        r.c[2][2] = b22 * invDet;
        return r;
    }

    // --- Multiplicações ---
    Mat3 operator*(const Mat3 &m) const
    {
        Mat3 r(NO_INIT);
        // r = this * m  (column-major)
        r.x[0] = x[0] * m.x[0] + x[3] * m.x[1] + x[6] * m.x[2];
        r.x[1] = x[1] * m.x[0] + x[4] * m.x[1] + x[7] * m.x[2];
        r.x[2] = x[2] * m.x[0] + x[5] * m.x[1] + x[8] * m.x[2];

        r.x[3] = x[0] * m.x[3] + x[3] * m.x[4] + x[6] * m.x[5];
        r.x[4] = x[1] * m.x[3] + x[4] * m.x[4] + x[7] * m.x[5];
        r.x[5] = x[2] * m.x[3] + x[5] * m.x[4] + x[8] * m.x[5];

        r.x[6] = x[0] * m.x[6] + x[3] * m.x[7] + x[6] * m.x[8];
        r.x[7] = x[1] * m.x[6] + x[4] * m.x[7] + x[7] * m.x[8];
        r.x[8] = x[2] * m.x[6] + x[5] * m.x[7] + x[8] * m.x[8];
        return r;
    }

    Mat3 operator*(float s) const
    {
        Mat3 r(*this);
        for (int i = 0; i < 9; ++i)
            r.x[i] *= s;
        return r;
    }

    // aplica 3x3 (sem translação)
    Vec3 operator*(const Vec3 &v) const
    {
        return Vec3(v.x * c[0][0] + v.y * c[1][0] + v.z * c[2][0],
                    v.x * c[0][1] + v.y * c[1][1] + v.z * c[2][1],
                    v.x * c[0][2] + v.y * c[1][2] + v.z * c[2][2]);
    }

    // --- utilidades ---
    void setCol(unsigned col, const Vec3 &v)
    {
        x[col * 3 + 0] = v.x;
        x[col * 3 + 1] = v.y;
        x[col * 3 + 2] = v.z;
    }
    Vec3 getCol(unsigned col) const
    {
        return Vec3(x[col * 3 + 0], x[col * 3 + 1], x[col * 3 + 2]);
    }
    Vec3 getRow(unsigned row) const
    {
        return Vec3(x[row + 0], x[row + 3], x[row + 6]);
    }

    // Ortonormaliza colunas (útil para TBN)
    Mat3 orthonormalized() const
    {
        Vec3 t = getCol(0), b = getCol(1), n = getCol(2);
        t = (t - n * Vec3::Dot(n, t)).normalized();
        b = Vec3::Cross(n, t).normalized();
        n = n.normalized();
        return FromColumns(t, b, n);
    }
};

// simétrico: escalar * Mat3
inline Mat3 operator*(float s, const Mat3 &m) { return m * s; }

// -------------------------------------------------------------------------------------------------
// Ray
// -------------------------------------------------------------------------------------------------
struct Ray
{
    Vec3 origin;
    Vec3 direction; // normalizada

    Ray() : origin(0, 0, 0), direction(1, 0, 0) {}
    Ray(const Vec3 &o, const Vec3 &d) : origin(o), direction(d)
    {
        direction.normalize();
    }

    Vec3 pointAt(float t) const { return origin + direction * t; }

    // Interseção raio-triângulo (Möller–Trumbore, non-culling)
    bool Intersection(const Vec3 &v0, const Vec3 &v1, const Vec3 &v2,
                      Vec3 &hitPoint) const
    {
        const float EPS = 1e-8f;

        const Vec3 e1 = v1 - v0;
        const Vec3 e2 = v2 - v0;

        const Vec3 pvec = Vec3::Cross(direction, e2);
        const float det = Vec3::Dot(e1, pvec);

        // paralelo ao plano do triângulo
        if (det > -EPS && det < EPS)
            return false;

        const float invDet = 1.0f / det;

        const Vec3 tvec = origin - v0;

        const float u = Vec3::Dot(tvec, pvec) * invDet;
        if (u < 0.0f || u > 1.0f)
            return false;

        const Vec3 qvec = Vec3::Cross(tvec, e1);
        const float v = Vec3::Dot(direction, qvec) * invDet;
        if (v < 0.0f || (u + v) > 1.0f)
            return false;

        const float t = Vec3::Dot(e2, qvec) * invDet;
        if (t < 0.0f)
            return false; // atrás da origem do raio

        hitPoint = origin + direction * t;
        return true;
    }

    // overload útil: devolve também t (e barycentrics
    bool Intersection(const Vec3 &v0, const Vec3 &v1, const Vec3 &v2,
                      float &tOut, float &uOut, float &vOut) const
    {
        const float EPS = 1e-8f;
        const Vec3 e1 = v1 - v0;
        const Vec3 e2 = v2 - v0;

        const Vec3 pvec = Vec3::Cross(direction, e2);
        const float det = Vec3::Dot(e1, pvec);
        if (det > -EPS && det < EPS)
            return false;

        const float invDet = 1.0f / det;
        const Vec3 tvec = origin - v0;

        const float u = Vec3::Dot(tvec, pvec) * invDet;
        if (u < 0.0f || u > 1.0f)
            return false;

        const Vec3 qvec = Vec3::Cross(tvec, e1);
        const float v = Vec3::Dot(direction, qvec) * invDet;
        if (v < 0.0f || (u + v) > 1.0f)
            return false;

        const float t = Vec3::Dot(e2, qvec) * invDet;
        if (t < 0.0f)
            return false;

        tOut = t;
        uOut = u;
        vOut = v;
        return true;
    }
    bool IntersectSphere(const Vec3 &center, float radius, float &tHit) const
    {
        Vec3 oc = origin - center;
        float b = Vec3::Dot(oc, direction);
        float c = Vec3::Dot(oc, oc) - radius * radius;
        float disc = b * b - c;
        if (disc < 0)
            return false;
        float s = sqrtf(disc);
        float t0 = -b - s, t1 = -b + s;
        float t = (t0 >= 0 ? t0 : t1);
        if (t < 0)
            return false;
        tHit = t;
        return true;
    }
};

// -------------------------------------------------------------------------------------------------
// Plane3D
// -------------------------------------------------------------------------------------------------

class Plane3D
{

private:
    float distToPoint(const Vec3 &point) const
    {
        return normal.dot(point) + dist;
    }

public:
    Vec3 normal;
    float dist;

    // ------------
    // Constructors
    // ------------
    Plane3D()
    {
        normal.x = 0;
        normal.y = 0;
        normal.z = 0;
        dist = 0;
    };

    explicit Plane3D(const float a, const float b, const float c, const float d)
    {
        normal = Vec3(a, b, c);
        float invLen = 1.0f / normal.length();
        normal *= invLen; // Normalize
        dist = d * invLen;
    }

    Plane3D(const Vec3 &v0, const Vec3 &v1, const Vec3 &v2)
    {
        normal = v1 - v0;
        normal = normal.cross(v2 - v0);
        normal.normalize();
        dist = -normal.dot(v0);
    }

    bool intersect(const Ray &ray, Vec3 &P) const
    {
        const float EPS = 1e-8f;
        float denom = Vec3::Dot(normal, ray.direction);
        if (fabsf(denom) < EPS)
            return false; // paralelo

        float t = -(Vec3::Dot(normal, ray.origin) + dist) / denom;
        if (t < 0.0f)
            return false; // atrás da origem

        P = ray.pointAt(t);
        return true;
    }

    float distanceToPoint(const Vec3 &p) const
    {
        return fabsf(Vec3::Dot(normal, p) + dist);
    }

    bool containsPoint(const Vec3 &p) const
    {
        return fabsf(Vec3::Dot(normal, p) + dist) < Epsilon;
    }

    Vec3 projectPoint(const Vec3 &p) const
    {
        float d = Vec3::Dot(normal, p) + dist;
        return p - normal * d;
    }
};

struct BoundingBox
{
    Vec3 min;
    Vec3 max;

    BoundingBox() : min(0, 0, 0), max(0, 0, 0) {}
    BoundingBox(const Vec3 &min, const Vec3 &max) : min(min), max(max) {}
    BoundingBox(const BoundingBox &box) : min(box.min), max(box.max) {}

    void Set(const Vec3 &min, const Vec3 &max)
    {
        this->min = min;
        this->max = max;
    }

    bool Merge(const Vec3 &_min, const Vec3 &_max)
    {
        bool changed = false;

        // Ignore zero-size boxes
        if (min == max)
        {
            changed = true;
            min = _min;
            max = _max;
        }
        else if (_min != _max)
        {
            if (_min.x < min.x)
            {
                changed = true;
                min.x = _min.x;
            }
            if (_min.y < min.y)
            {
                changed = true;
                min.y = _min.y;
            }
            if (_min.z < min.z)
            {
                changed = true;
                min.z = _min.z;
            }

            if (_max.x > max.x)
            {
                changed = true;
                max.x = _max.x;
            }
            if (_max.y > max.y)
            {
                changed = true;
                max.y = _max.y;
            }
            if (_max.z > max.z)
            {
                changed = true;
                max.z = _max.z;
            }
        }

        return changed;
    }
    bool Merge(const BoundingBox &b)
    {
        bool changed = false;

        // Ignore zero-size boxes
        if (min == max)
        {
            changed = true;
            min = b.min;
            max = b.max;
        }
        else if (b.min != b.max)
        {
            if (b.min.x < min.x)
            {
                changed = true;
                min.x = b.min.x;
            }
            if (b.min.y < min.y)
            {
                changed = true;
                min.y = b.min.y;
            }
            if (b.min.z < min.z)
            {
                changed = true;
                min.z = b.min.z;
            }

            if (b.max.x > max.x)
            {
                changed = true;
                max.x = b.max.x;
            }
            if (b.max.y > max.y)
            {
                changed = true;
                max.y = b.max.y;
            }
            if (b.max.z > max.z)
            {
                changed = true;
                max.z = b.max.z;
            }
        }

        return changed;
    }

    void AddPoint(const Vec3 &point)
    {
        min = min.Min(point);
        max = max.Max(point);
    }
    void Clear()
    {
        min = Vec3(0, 0, 0);
        max = Vec3(0, 0, 0);
    }

    Vec3 GetCorner(u32 index) const
    {
        switch (index)
        {
        case 0:
            return Vec3(min.x, min.y, max.z);
        case 1:
            return Vec3(max.x, min.y, max.z);
        case 2:
            return Vec3(max.x, max.y, max.z);
        case 3:
            return Vec3(min.x, max.y, max.z);
        case 4:
            return Vec3(min.x, min.y, min.z);
        case 5:
            return Vec3(max.x, min.y, min.z);
        case 6:
            return Vec3(max.x, max.y, min.z);
        case 7:
            return Vec3(min.x, max.y, min.z);
        default:
            return Vec3();
        }
    }

    void Transform(const Mat4 &m)
    {
        // Efficient algorithm for transforming an AABB, taken from Graphics
        // Gems

        float minA[3] = {min.x, min.y, min.z}, minB[3];
        float maxA[3] = {max.x, max.y, max.z}, maxB[3];

        for (u32 i = 0; i < 3; ++i)
        {
            minB[i] = m.c[3][i];
            maxB[i] = m.c[3][i];

            for (u32 j = 0; j < 3; ++j)
            {
                float x = minA[j] * m.c[j][i];
                float y = maxA[j] * m.c[j][i];
                minB[i] += Min(x, y);
                maxB[i] += Max(x, y);
            }
        }

        min = Vec3(minB[0], minB[1], minB[2]);
        max = Vec3(maxB[0], maxB[1], maxB[2]);
    }

    bool Intersection(const Ray &ray)
    {
        // SLAB based optimized ray/AABB intersection routine
        // Idea taken from http://ompf.org/ray/

        float l1 = (min.x - ray.origin.x) / ray.direction.x;
        float l2 = (max.x - ray.origin.x) / ray.direction.x;
        float lmin = Min(l1, l2);
        float lmax = Max(l1, l2);

        l1 = (min.y - ray.origin.y) / ray.direction.y;
        l2 = (max.y - ray.origin.y) / ray.direction.y;
        lmin = Max(Min(l1, l2), lmin);
        lmax = Min(Max(l1, l2), lmax);

        l1 = (min.z - ray.origin.z) / ray.direction.z;
        l2 = (max.z - ray.origin.z) / ray.direction.z;
        lmin = Max(Min(l1, l2), lmin);
        lmax = Min(Max(l1, l2), lmax);

        if ((lmax >= 0.0f) & (lmax >= lmin))
        {
            // Consider length
            const Vec3 rayDest = ray.origin + ray.direction;
            Vec3 rayMins(Min(rayDest.x, ray.origin.x),
                         Min(rayDest.y, ray.origin.y),
                         Min(rayDest.z, ray.origin.z));
            Vec3 rayMaxs(Max(rayDest.x, ray.origin.x),
                         Max(rayDest.y, ray.origin.y),
                         Max(rayDest.z, ray.origin.z));
            return (rayMins.x < max.x) && (rayMaxs.x > min.x) && (rayMins.y < max.y) && (rayMaxs.y > min.y) && (rayMins.z < max.z) && (rayMaxs.z > min.z);
        }
        else
            return false;
    }

    float Distance(const Vec3 &pos)
    {
        const Vec3 center = (min + max) * 0.5f;
        const Vec3 extent = (max - min) * 0.5f;

        Vec3 nearestVec;
        nearestVec.x = Max(0.0f, fabsf(pos.x - center.x) - extent.x);
        nearestVec.y = Max(0.0f, fabsf(pos.y - center.y) - extent.y);
        nearestVec.z = Max(0.0f, fabsf(pos.z - center.z) - extent.z);

        return nearestVec.length();
    }

    BoundingBox &operator=(const BoundingBox &box)
    {
        if (this == &box)
            return *this;
        min = box.min;
        max = box.max;
        return *this;
    }

    static BoundingBox TransformBoundingBox(const BoundingBox &box,
                                            const Mat4 &m)
    {
        BoundingBox result(box);
        result.Transform(m);
        return result;
    }
    static void TransformBoundingBox(const BoundingBox &box, const Mat4 &m,
                                     BoundingBox &out)
    {
        out.Merge(box);
        out.Transform(m);
    }

    Vec3 corner(int n) const
    {
        return Vec3((n & 1) ? max.x : min.x, (n & 2) ? max.y : min.y,
                    (n & 4) ? max.z : min.z);
    }

    Vec3 center() const { return (min + max) * 0.5f; }

    // AABB vs AABB (sobreposição em todos os eixos).
    bool intersects(const BoundingBox &b) const
    {
        return min.x <= b.max.x && max.x >= b.min.x && min.y <= b.max.y &&
               max.y >= b.min.y && min.z <= b.max.z && max.z >= b.min.z;
    }

    // Raio vs AABB (slab). Devolve t de entrada (>=0) ou -1 se não interseta.
    float intersects_ray(const Vec3 &o, const Vec3 &d) const
    {
        float tmin = 0.0f, tmax = 1e30f;
        for (int i = 0; i < 3; ++i)
        {
            float oi = (&o.x)[i], di = (&d.x)[i];
            float mn = (&min.x)[i], mx = (&max.x)[i];
            if (fabsf(di) < 1e-8f) { if (oi < mn || oi > mx) return -1.0f; }
            else
            {
                float inv = 1.0f / di;
                float t1 = (mn - oi) * inv, t2 = (mx - oi) * inv;
                if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
                tmin = t1 > tmin ? t1 : tmin;
                tmax = t2 < tmax ? t2 : tmax;
                if (tmin > tmax) return -1.0f;
            }
        }
        return tmin;
    }
};

// Triângulo (colisão / spatial trees). v0,v1,v2 + helpers.
struct Triangle
{
    Vec3 v0, v1, v2;
    Triangle() : v0(0, 0, 0), v1(0, 0, 0), v2(0, 0, 0) {}
    Triangle(const Vec3 &a, const Vec3 &b, const Vec3 &c) : v0(a), v1(b), v2(c) {}

    Vec3 center() const { return (v0 + v1 + v2) * (1.0f / 3.0f); }
    Vec3 normal() const
    {
        Vec3 n = Vec3::Cross(v1 - v0, v2 - v0);
        float l2 = n.length_squared();
        return (l2 > 1e-12f) ? n * (1.0f / sqrtf(l2)) : Vec3(0, 1, 0);
    }
    BoundingBox bounds() const
    {
        Vec3 mn = v0.Min(v1).Min(v2);
        Vec3 mx = v0.Max(v1).Max(v2);
        return BoundingBox(mn, mx);
    }
};

class Frustum
{
public:
    const Vec3 &getOrigin() const { return m_origin; }
    const Vec3 &getCorner(u32 index) const { return m_corners[index]; }

    void build(const Mat4 &transMat, float left, float right, float bottom,
               float top, float nearPlane, float farPlane)
    {
        float left_f = left * farPlane / nearPlane;
        float right_f = right * farPlane / nearPlane;
        float bottom_f = bottom * farPlane / nearPlane;
        float top_f = top * farPlane / nearPlane;

        m_corners[0] = Vec3(left, bottom, -nearPlane);
        m_corners[1] = Vec3(right, bottom, -nearPlane);
        m_corners[2] = Vec3(right, top, -nearPlane);
        m_corners[3] = Vec3(left, top, -nearPlane);

        m_corners[4] = Vec3(left_f, bottom_f, -farPlane);
        m_corners[5] = Vec3(right_f, bottom_f, -farPlane);
        m_corners[6] = Vec3(right_f, top_f, -farPlane);
        m_corners[7] = Vec3(left_f, top_f, -farPlane);

        // Transform points to fit camera position and rotation
        m_origin = transMat * Vec3(0, 0, 0);
        for (u32 i = 0; i < 8; ++i)
            m_corners[i] = transMat * m_corners[i];

        m_planes[0] = Plane3D(m_origin, m_corners[3], m_corners[0]);     // Left
        m_planes[1] = Plane3D(m_origin, m_corners[1], m_corners[2]);     // Right
        m_planes[2] = Plane3D(m_origin, m_corners[0], m_corners[1]);     // Bottom
        m_planes[3] = Plane3D(m_origin, m_corners[2], m_corners[3]);     // Top
        m_planes[4] = Plane3D(m_corners[0], m_corners[1], m_corners[2]); // Near
        m_planes[5] = Plane3D(m_corners[5], m_corners[4], m_corners[7]); // Far
    }

    void build(const Mat4 &transMat, float fov, float aspect, float nearPlane,
               float farPlane)
    {
        float ymax = nearPlane * tanf(degToRad(fov / 2));
        float xmax = ymax * aspect;

        build(transMat, -xmax, xmax, -ymax, ymax, nearPlane, farPlane);
    }
    void build(const Mat4 &viewMat, const Mat4 &projMat)
    {
        // This routine works with the OpenGL projection matrix
        // The view matrix is the inverse camera transformation matrix
        // Note: Frustum corners are not updated!

        Mat4 m = projMat * viewMat;

        m_planes[0] =
            Plane3D(-(m.c[0][3] + m.c[0][0]), -(m.c[1][3] + m.c[1][0]),
                    -(m.c[2][3] + m.c[2][0]), -(m.c[3][3] + m.c[3][0])); // Left
        m_planes[1] = Plane3D(
            -(m.c[0][3] - m.c[0][0]), -(m.c[1][3] - m.c[1][0]),
            -(m.c[2][3] - m.c[2][0]), -(m.c[3][3] - m.c[3][0])); // Right
        m_planes[2] = Plane3D(
            -(m.c[0][3] + m.c[0][1]), -(m.c[1][3] + m.c[1][1]),
            -(m.c[2][3] + m.c[2][1]), -(m.c[3][3] + m.c[3][1])); // Bottom
        m_planes[3] =
            Plane3D(-(m.c[0][3] - m.c[0][1]), -(m.c[1][3] - m.c[1][1]),
                    -(m.c[2][3] - m.c[2][1]), -(m.c[3][3] - m.c[3][1])); // Top
        m_planes[4] =
            Plane3D(-(m.c[0][3] + m.c[0][2]), -(m.c[1][3] + m.c[1][2]),
                    -(m.c[2][3] + m.c[2][2]), -(m.c[3][3] + m.c[3][2])); // Near
        m_planes[5] =
            Plane3D(-(m.c[0][3] - m.c[0][2]), -(m.c[1][3] - m.c[1][2]),
                    -(m.c[2][3] - m.c[2][2]), -(m.c[3][3] - m.c[3][2])); // Far

        m_origin = viewMat.inverted() * Vec3(0, 0, 0);

        // Calculate corners
        Mat4 mm = m.inverted();
        Vec4 corner = mm * Vec4(-1, -1, -1, 1);
        m_corners[0] =
            Vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
        corner = mm * Vec4(1, -1, -1, 1);
        m_corners[1] =
            Vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
        corner = mm * Vec4(1, 1, -1, 1);
        m_corners[2] =
            Vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
        corner = mm * Vec4(-1, 1, -1, 1);
        m_corners[3] =
            Vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
        corner = mm * Vec4(-1, -1, 1, 1);
        m_corners[4] =
            Vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
        corner = mm * Vec4(1, -1, 1, 1);
        m_corners[5] =
            Vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
        corner = mm * Vec4(1, 1, 1, 1);
        m_corners[6] =
            Vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
        corner = mm * Vec4(-1, 1, 1, 1);
        m_corners[7] =
            Vec3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
    }
    bool SphereInside(Vec3 pos, float rad) const
    {
        for (u32 i = 0; i < 6; ++i)
        {
            if (m_planes[i].distanceToPoint(pos) > rad)
                return true;
        }

        return false;
    }
    bool BoxInside(BoundingBox &b) const
    {
        for (u32 i = 0; i < 6; ++i)
        {
            const Vec3 &n = m_planes[i].normal;

            Vec3 positive = b.min;
            if (n.x <= 0)
                positive.x = b.max.x;
            if (n.y <= 0)
                positive.y = b.max.y;
            if (n.z <= 0)
                positive.z = b.max.z;

            if (m_planes[i].distanceToPoint(positive) > 0)
                return true;
        }

        return false;
    }

    // Teste AABB-vs-frustum correto (signed distance + p-vertex). Devolve true
    // se a caixa está dentro ou interseta o frustum; false se totalmente fora.
    // (distanceToPoint do Plane3D é |.|, por isso usamos a distância com sinal.)
    bool ContainsBox(const BoundingBox &box) const
    {
        // Os planos deste Frustum apontam para FORA (interior tem s<0), por isso
        // a caixa está toda fora se o n-vertex (o vértice mais contra a normal)
        // ainda está do lado de fora (s>0).
        for (u32 i = 0; i < 6; ++i)
        {
            const Vec3 &n = m_planes[i].normal;
            Vec3 p;
            p.x = (n.x >= 0.0f) ? box.min.x : box.max.x;
            p.y = (n.y >= 0.0f) ? box.min.y : box.max.y;
            p.z = (n.z >= 0.0f) ? box.min.z : box.max.z;
            if (Vec3::Dot(n, p) + m_planes[i].dist > 0.0f)
                return false;
        }
        return true;
    }

    bool PointInside(const Vec3 &point) const
    {
        for (u32 i = 0; i < 6; ++i)
        {
            const Plane3D &plane = m_planes[i];
            if (plane.distanceToPoint(point) < 0)
            {
                return false;
            }
        }
        return true;
    }

    void getAABB(Vec3 &mins, Vec3 &maxs) const
    {
        mins.x = MaxFloat;
        mins.y = MaxFloat;
        mins.z = MaxFloat;
        maxs.x = -MaxFloat;
        maxs.y = -MaxFloat;
        maxs.z = -MaxFloat;

        for (u32 i = 0; i < 8; ++i)
        {
            if (m_corners[i].x < mins.x)
                mins.x = m_corners[i].x;
            if (m_corners[i].y < mins.y)
                mins.y = m_corners[i].y;
            if (m_corners[i].z < mins.z)
                mins.z = m_corners[i].z;
            if (m_corners[i].x > maxs.x)
                maxs.x = m_corners[i].x;
            if (m_corners[i].y > maxs.y)
                maxs.y = m_corners[i].y;
            if (m_corners[i].z > maxs.z)
                maxs.z = m_corners[i].z;
        }
    }
    void getAABB(BoundingBox &b) const
    {
        Vec3 mins, maxs;
        getAABB(mins, maxs);
        b.min = mins;
        b.max = maxs;
    }

private:
    Plane3D m_planes[6];
    Vec3 m_origin;
    Vec3 m_corners[8];
};

template <typename T>
struct Rectangle
{

    T x;
    T y;
    T width;
    T height;

    Rectangle() : x(0), y(0), width(0), height(0) {}
    Rectangle(T x, T y, T width, T height)
        : x(x), y(y), width(width), height(height)
    {
    }
    Rectangle(const Rectangle &rect)
        : x(rect.x), y(rect.y), width(rect.width), height(rect.height)
    {
    }

    void Set(T x, T y, T width, T height)
    {
        this->x = x;
        this->y = y;
        this->width = width;
        this->height = height;
    }

    void Merge(const Rectangle &rect)
    {
        T right = x + width;
        T bottom = y + height;
        T rectRight = rect.x + rect.width;
        T rectBottom = rect.y + rect.height;
        x = Min(x, rect.x);
        y = Min(y, rect.y);
        right = Max(right, rectRight);
        bottom = Max(bottom, rectBottom);
        width = right - x;
        height = bottom - y;
    }

    void Merge(const Vec2 &point)
    {
        T right = x + width;
        T bottom = y + height;
        x = Min(x, point.x);
        y = Min(y, point.y);
        right = Max(right, point.x);
        bottom = Max(bottom, point.y);
        width = right - x;
        height = bottom - y;
    }

    void Clear()
    {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
    }

    Rectangle &operator=(const Rectangle &rect)
    {
        if (this == &rect)
            return *this;
        x = rect.x;
        y = rect.y;
        width = rect.width;
        height = rect.height;
        return *this;
    }
};

template <typename T>
struct Size
{
    T width;
    T height;

    Size() : width(0), height(0) {}
    Size(T w, T h) : width(w), height(h) {}
    Size(const Size &size) : width(size.width), height(size.height) {}

    Size &operator=(const Size &size)
    {
        if (this == &size)
            return *this;
        width = size.width;
        height = size.height;
        return *this;
    }
};

typedef Rectangle<int> IntRect;
typedef Rectangle<float> FloatRect;
typedef Size<int> IntSize;
typedef Size<float> FloatSize;
