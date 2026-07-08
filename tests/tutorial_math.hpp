#pragma once

// Shared by the 3D tutorials: coregl has no math types on purpose, so these
// are the handful of column-major 4x4 helpers needed to move a camera and a
// model around. Bring whatever math library you like in a real project —
// this exists only so the tutorials don't repeat the same 40 lines each.

#include <cmath>

inline void tut_mat4_identity(float* m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = 0.f;
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

inline void tut_mat4_mul(float* out, const float* a, const float* b) // out = a * b
{
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 4; ++i)
            r[c * 4 + i] = a[i] * b[c * 4] + a[4 + i] * b[c * 4 + 1] + a[8 + i] * b[c * 4 + 2] +
                           a[12 + i] * b[c * 4 + 3];
    for (int i = 0; i < 16; ++i)
        out[i] = r[i];
}

inline void tut_mat4_perspective(float* m, float fovyDeg, float aspect, float zNear, float zFar)
{
    for (int i = 0; i < 16; ++i)
        m[i] = 0.f;
    const float f = 1.f / tanf(fovyDeg * 0.00872664626f); // (fovyDeg/2) in radians
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.f;
    m[14] = 2.f * zFar * zNear / (zNear - zFar);
}

inline void tut_mat4_translate(float* m, float x, float y, float z)
{
    tut_mat4_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

inline void tut_mat4_rotate_xy(float* m, float xDeg, float yDeg)
{
    float rx[16], ry[16];
    tut_mat4_identity(rx);
    float cx = cosf(xDeg * 0.01745329252f), sx = sinf(xDeg * 0.01745329252f);
    rx[5] = cx;
    rx[6] = sx;
    rx[9] = -sx;
    rx[10] = cx;

    tut_mat4_identity(ry);
    float cy = cosf(yDeg * 0.01745329252f), sy = sinf(yDeg * 0.01745329252f);
    ry[0] = cy;
    ry[2] = -sy;
    ry[8] = sy;
    ry[10] = cy;

    tut_mat4_mul(m, ry, rx);
}
