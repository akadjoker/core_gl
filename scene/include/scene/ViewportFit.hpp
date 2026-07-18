#pragma once

// Fits a fixed virtual resolution into an arbitrary window size, three ways:
//  - Stretch:   viewport = the whole window, non-uniform scale (distorts).
//  - Letterbox: uniform scale to the largest rect that FITS inside the
//    window (min of the two axis scales), centered — black bars fill
//    whatever's left over on the short axis.
//  - Crop:      uniform scale to the smallest rect that COVERS the window
//    (max of the two axis scales), centered — the overflow is simply
//    outside the window, which GL already clips for free.
//
// All three keep the same camera aspect (virtualW/virtualH, passed to
// SceneRenderer::render_fit as the virtual resolution) baked into the
// projection matrix — only the GL viewport rect differs. There's no
// separate render target or blit step for any of them: a GL viewport rect
// already does an independent-per-axis scale from clip space, which is
// exactly what Stretch needs, and one bigger than the framebuffer (Crop)
// is legal and silently clipped by the driver.
//
// compute_viewport_fit() is pure math  so game code can reuse it for
// mouse-picking math (converting window pixel coords into the actual
// rendered content rect) without depending on SceneRenderer for it — see
// SceneRenderer::render_fit() for the GL-applying half (viewport + bars).

// global scope, like Scene/Camera3D/SceneRenderer and the rest of the
// scene-graph API — the real `namespace scene {}` (ByteArray, Pixmap,
// Filesystem's `fs::`, ...) is reserved for lower-level utilities.
enum class ViewportFitMode
{
    Stretch,
    Letterbox,
    Crop
};

struct ViewportRect
{
    int x, y, w, h;
};

inline ViewportRect compute_viewport_fit(int windowW, int windowH, int virtualW, int virtualH,
                                         ViewportFitMode mode)
{
    if (mode == ViewportFitMode::Stretch || virtualW <= 0 || virtualH <= 0)
        return ViewportRect{0, 0, windowW, windowH};

    float sx = (float)windowW / (float)virtualW;
    float sy = (float)windowH / (float)virtualH;
    float s = (mode == ViewportFitMode::Letterbox) ? (sx < sy ? sx : sy) : (sx > sy ? sx : sy);
    int w = (int)(virtualW * s + 0.5f);
    int h = (int)(virtualH * s + 0.5f);
    ViewportRect r;
    r.w = w;
    r.h = h;
    r.x = (windowW - w) / 2;
    r.y = (windowH - h) / 2;
    return r;
}
