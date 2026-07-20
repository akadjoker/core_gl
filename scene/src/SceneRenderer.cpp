#include <chrono>
#include "scene/SceneRenderer.hpp"
#include "scene/Camera3D.hpp"
#include "scene/LightNode.hpp"
#include "scene/Material.hpp"
#include "scene/DecalSystemNode.hpp"
#include "scene/GrassSystemNode.hpp"
#include "scene/TreeSystemNode.hpp"
#include "scene/OceanNode.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/BillboardNode.hpp"
#include "scene/RibbonTrailNode.hpp"
#include "scene/TerrainPagingNode.hpp"
#include "scene/MeshInstance.hpp"
#include "scene/SkinnedMesh.hpp"
#include "scene/SkinnedMeshInstance.hpp"
#include "scene/BspInstance.hpp"
#include "scene/LensFlareNode.hpp"
#include "scene/Camera3D.hpp"
#include "scene/Pixmap.hpp"
#include "scene/WaterNode.hpp"
#include "scene/MirrorNode.hpp"
#include <coregl/gl_framebuffer.hpp>
#include <coregl/gl_renderer.hpp>
#include <coregl/gl_vertex_array.hpp>
#include <cstdio>
#include <cstdlib>

#include "SceneShaders.hpp"

// ── CSM cascade fitting ──────────────────────────────────────────────────

// split distances: log/uniform blend (finer slices near the camera).
// lambda=0.5 fixed (was a runtime knob there; no caller ever changed it).
static void csmSplits(float nearClip, float farClip, float* splits, int numCascades)
{
    const float lambda = 0.5f;
    splits[0] = nearClip;
    for (int i = 1; i <= numCascades; ++i)
    {
        float p = (float)i / (float)numCascades;
        float logS = nearClip * powf(farClip / nearClip, p);
        float uniS = nearClip + (farClip - nearClip) * p;
        splits[i] = lambda * logS + (1.0f - lambda) * uniS;
    }
}

// Fraction of the scene's own shadow_distance to guarantee as Z-padding
// per cascade, so casters standing outside a slice's immediate view frustum
// still get caught. These were originally fixed absolute world units
// (80/120/180/250, i.e. exactly this ratio of the 300-unit default
// shadow_distance most demos pass) — fine at that scale, but a fixed
// absolute floor is scale-blind: a scene whose whole map is, say, 188
// units across (a human-scale level, not a vehicle-scale one) got at
// least 80 units of padding on cascade 0 alone, several times its own
// size, which starves the near cascade's depth-buffer precision exactly
// where receivers need it most (no per-pixel bias can compensate for
// that — the precision is gone before biasing even applies).
static float casterDistanceForCascade(int c, float shadowDistance)
{
    static constexpr float kMinCasterFrac[] = {0.27f, 0.4f, 0.6f, 0.83f};

    constexpr int count = int(sizeof(kMinCasterFrac) / sizeof(kMinCasterFrac[0]));

    return kMinCasterFrac[Clamp(c, 0, count - 1)] * shadowDistance;
}
 
// 1. this slice's 8 frustum corners, straight from tan(fov/2) + camera
//    world transform (no projection-matrix inverse needed)
// 2. bounding SPHERE around them: the radius depends only on the slice's
//    shape (fov/aspect/near/far), never on camera orientation, so it's
//    identical every frame — quantized to 1/16 for extra float stability
// 3. light eye placed a full RADIUS back from the center (not a unit
//    lightDir step) — this is what makes the Z fit below well-behaved
// 4. Z range from the real corners in light space, padded for casters
//    outside the slice
// 5. texel-snap by nudging the ortho's translation so world-space origin
//    (a fixed reference point, not the moving center) lands on a texel
static Mat4 csmCascadeMatrix(int cascade, float aspect, float fovDeg, const Mat4& view,
                             const float* splits, const Vec3& lightDir, float shadowMapSize,
                             float shadowDistance, float& outTexelWorldSize)
{
    const float zn = splits[cascade], zf = splits[cascade + 1];
    const Mat4 invView = Mat4::Inverse(view); // camera -> world
    const float tanV = tanf(fovDeg * 0.5f * 3.14159265f / 180.f);
    const float tanH = tanV * aspect;
    const Vec3 L = lightDir.normalized();
    const Vec3 up = (fabsf(L.y) > 0.99f) ? Vec3(0.f, 0.f, 1.f) : Vec3(0.f, 1.f, 0.f);

    Vec3 corners[8];
    int k = 0;
    for (int fi = 0; fi < 2; ++fi)
    {
        float z = (fi == 0) ? zn : zf;
        float x = z * tanH, y = z * tanV;
        Vec3 vs[4] = {Vec3(-x, -y, -z), Vec3(x, -y, -z), Vec3(x, y, -z), Vec3(-x, y, -z)};
        for (int j = 0; j < 4; ++j)
            corners[k++] = invView * vs[j];
    }

    Vec3 center(0.f, 0.f, 0.f);
    for (int j = 0; j < 8; ++j)
        center += corners[j];
    center *= (1.0f / 8.0f);
    float r = 0.f;
    for (int j = 0; j < 8; ++j)
        r = std::max(r, (corners[j] - center).length());
    r = ceilf(r * 16.0f) / 16.0f; // quantize for extra stability

    // one shadow-map texel's world-space footprint for THIS cascade — the
    // ortho spans [-r, r] (width 2r) across shadowMapSize texels. Grows
    // with cascade distance automatically (farther cascades cover more
    // world per texel), so a bias expressed as "N texels" self-scales
    // with both cascade and overall scene scale, unlike a fixed world-unit
    // constant.
    outTexelWorldSize = (2.0f * r) / shadowMapSize;

    Mat4 lightView = Mat4::LookAt(center - L * r, center, up);

    float zmin = 1e30f, zmax = -1e30f;
    for (int j = 0; j < 8; ++j)
    {
        float z = (lightView * corners[j]).z;
        zmin = std::min(zmin, z);
        zmax = std::max(zmax, z);
    }
    //   float zpad = (zmax - zmin) * 0.5f + 5.0f;
    // float zpad = std::max((zmax - zmin) * 0.5f + 5.0f, 100.0f);

    float zpad = std::max((zmax - zmin) * 0.5f + 5.0f, casterDistanceForCascade(cascade, shadowDistance));

    Mat4 lightProj = Mat4::Ortho(-r, r, -r, r, -zmax - zpad, -zmin);

    // snap against world-space origin's projected position, not the
    // cascade center — a fixed reference point makes the snap itself
    // stable regardless of how the center moves frame to frame
    Mat4 sm = lightProj * lightView;
    Vec3 origin = sm * Vec3(0.f, 0.f, 0.f);
    float half = shadowMapSize * 0.5f;
    float ox = origin.x * half, oy = origin.y * half;
    lightProj.c[3][0] += (roundf(ox) - ox) / half;
    lightProj.c[3][1] += (roundf(oy) - oy) / half;

    return lightProj * lightView;
}

// prepends the platform shader header to a version-less body
static bool loadStage(gl::Shader& shader, gl::PipelineStage stage, const char* body)
{
    std::string src = std::string(gl::Renderer::ShaderHeader(stage)) + body;
    return shader.LoadFromString(stage, src.c_str());
}

bool SceneRenderer::init()
{
    if (!loadStage(m_forward, gl::PipelineStage::VERTEX, kFwdVS) ||
        !loadStage(m_forward, gl::PipelineStage::FRAGMENT, kFwdFS) || !m_forward.Link())
        return false;
    if (!loadStage(m_bsp, gl::PipelineStage::VERTEX, kBSP_VS) ||
        !loadStage(m_bsp, gl::PipelineStage::FRAGMENT, kBSP_FS) || !m_bsp.Link())
        return false;
    if (!loadStage(m_water, gl::PipelineStage::VERTEX, kWaterVS) ||
        !loadStage(m_water, gl::PipelineStage::FRAGMENT, kWaterFS) || !m_water.Link())
        return false;
    if (!loadStage(m_mirror, gl::PipelineStage::VERTEX, kMirrorVS) ||
        !loadStage(m_mirror, gl::PipelineStage::FRAGMENT, kMirrorFS) || !m_mirror.Link())
        return false;

    m_locModel = m_forward.GetLocation("u_model");
    m_locViewProj = m_forward.GetLocation("u_viewProj");
    m_locView = m_forward.GetLocation("u_view");
    m_locColor = m_forward.GetLocation("u_baseColor");
    m_locLightDir = m_forward.GetLocation("u_lightDir");
    m_locAmbient = m_forward.GetLocation("u_ambient");
    m_locClipPlane = m_forward.GetLocation("u_clipPlane");
    m_locUnlit = m_forward.GetLocation("u_unlit");
    m_locCameraPos = m_forward.GetLocation("u_cameraPos");
    m_locSpecular = m_forward.GetLocation("u_specular");
    // introspection reports arrays as "name[0]"; consecutive elements occupy
    // consecutive locations
    m_locCascadeMat0 = m_forward.GetLocation("u_lightViewProj[0]");
    m_locSplits0 = m_forward.GetLocation("u_splits[0]");
    m_locCascadeCount = m_forward.GetLocation("u_cascadeCount");
    m_locShowCascades = m_forward.GetLocation("u_showCascades");
    m_locShadowSize = m_forward.GetLocation("u_shadowMapSize");
    m_locShadowNormalBias = m_forward.GetLocation("u_shadowNormalBias");
    m_locDebugShadowClip = m_forward.GetLocation("u_debugShadowClip");
    m_locPointCount = m_forward.GetLocation("u_pointCount");
    m_locPointPosRange0 = m_forward.GetLocation("u_pointPosRange[0]");
    m_locPointColor0 = m_forward.GetLocation("u_pointColor[0]");
    m_locSpotCount = m_forward.GetLocation("u_spotCount");
    m_locSpotPosRange0 = m_forward.GetLocation("u_spotPosRange[0]");
    m_locSpotDirInner0 = m_forward.GetLocation("u_spotDirInner[0]");
    m_locSpotColorOuter0 = m_forward.GetLocation("u_spotColorOuter[0]");
    m_locSpotShadowSlot0 = m_forward.GetLocation("u_spotShadowSlot[0]");
    m_locSpotShadowMat0 = m_forward.GetLocation("u_spotShadowMat[0]");
    m_forward.Bind();
    m_forward.SetInt("u_diffuse", 0);
    m_forward.SetInt("u_shadowMap", 1);
    m_forward.SetInt("u_detail", 6);
    m_forward.SetInt("u_pointShadow0", 2);
    m_forward.SetInt("u_pointShadow1", 3);
    m_forward.SetInt("u_spotShadow0", 4);
    m_forward.SetInt("u_spotShadow1", 5);
    m_forward.SetInt(m_locCascadeCount, 0);
    m_forward.SetInt(m_locPointCount, 0);
    m_forward.SetInt(m_locSpotCount, 0);

    // BSP lightmapped shader uniforms (reuse texture slots 0 + 6)
    m_locBspModel     = m_bsp.GetLocation("u_model");
    m_locBspViewProj  = m_bsp.GetLocation("u_viewProj");
    m_locBspView      = m_bsp.GetLocation("u_view");
    m_locBspColor     = m_bsp.GetLocation("u_baseColor");
    m_locBspLightDir  = m_bsp.GetLocation("u_lightDir");
    m_locBspClipPlane = m_bsp.GetLocation("u_clipPlane");
    m_locBspCameraPos = m_bsp.GetLocation("u_cameraPos");
    m_bsp.Bind();
    m_bsp.SetInt("u_diffuse", 0);
    m_bsp.SetInt("u_detail", 6);

    if (!loadStage(m_pointDepth, gl::PipelineStage::VERTEX, kPointDepthVS) ||
        !loadStage(m_pointDepth, gl::PipelineStage::FRAGMENT, kPointDepthFS) ||
        !m_pointDepth.Link())
        return false;
    m_locPDModel = m_pointDepth.GetLocation("u_model");
    m_locPDLightVP = m_pointDepth.GetLocation("u_lightVP");
    m_locPDLightPos = m_pointDepth.GetLocation("u_lightPos");
    m_locPDRange = m_pointDepth.GetLocation("u_range");

    m_locWModel = m_water.GetLocation("u_model");
    m_locWViewProj = m_water.GetLocation("u_viewProj");
    m_locWCameraPos = m_water.GetLocation("u_cameraPos");
    m_locWLightDir = m_water.GetLocation("u_lightDir");
    m_locWTime = m_water.GetLocation("u_time");
    m_locWColor = m_water.GetLocation("u_waterColor");
    m_locWDistortion = m_water.GetLocation("u_distortion");
    m_locWTiling = m_water.GetLocation("u_tiling");
    m_locWColorMix = m_water.GetLocation("u_colorMix");
    m_locWCamPlanes = m_water.GetLocation("u_camPlanes");
    m_water.Bind();
    m_water.SetInt("u_reflection", 0);
    m_water.SetInt("u_refraction", 1);
    m_water.SetInt("u_refractionDepth", 2);

    m_locMModel = m_mirror.GetLocation("u_model");
    m_locMViewProj = m_mirror.GetLocation("u_viewProj");
    m_locMCameraPos = m_mirror.GetLocation("u_cameraPos");
    m_locMTint = m_mirror.GetLocation("u_tint");
    m_locMReflectivity = m_mirror.GetLocation("u_reflectivity");
    m_locMNormal = m_mirror.GetLocation("u_normal");
    m_mirror.Bind();
    m_mirror.SetInt("u_reflection", 0);

    if (!m_debug.LoadFromString(gl::PipelineStage::VERTEX, gl::Renderer::QuadShaderSource()) ||
        !loadStage(m_debug, gl::PipelineStage::FRAGMENT, kDebugFS) || !m_debug.Link())
        return false;
    m_locDRect = m_debug.GetLocation("u_rect");
    m_locDTargetSize = m_debug.GetLocation("u_targetSize");
    m_debug.Bind();
    m_debug.SetInt("u_tex", 0);

    if (!loadStage(m_skyboxShader, gl::PipelineStage::VERTEX, kSkyVS) ||
        !loadStage(m_skyboxShader, gl::PipelineStage::FRAGMENT, kSkyboxFS) ||
        !m_skyboxShader.Link())
        return false;
    m_skyboxShader.Bind();
    m_skyboxShader.SetInt("u_sky", 0);
    if (!loadStage(m_skydomeShader, gl::PipelineStage::VERTEX, kSkyVS) ||
        !loadStage(m_skydomeShader, gl::PipelineStage::FRAGMENT, kSkydomeFS) ||
        !m_skydomeShader.Link())
        return false;
    m_skydomeShader.Bind();
    m_skydomeShader.SetInt("u_sky", 0);
    if (!loadStage(m_sky, gl::PipelineStage::VERTEX, kSkyVS) ||
        !loadStage(m_sky, gl::PipelineStage::FRAGMENT, kSkyFS) || !m_sky.Link())
        return false;

    if (!loadStage(m_particle, gl::PipelineStage::VERTEX, kParticleVS) ||
        !loadStage(m_particle, gl::PipelineStage::FRAGMENT, kParticleFS) || !m_particle.Link())
        return false;
    m_locPViewProj = m_particle.GetLocation("u_viewProj");
    m_particle.Bind();
    m_particle.SetInt("u_tex", 0);

    if (!loadStage(m_grass, gl::PipelineStage::VERTEX, kGrassVS) ||
        !loadStage(m_grass, gl::PipelineStage::FRAGMENT, kGrassFS) || !m_grass.Link())
        return false;
    m_locGViewProj = m_grass.GetLocation("u_viewProj");
    m_grass.Bind();
    m_grass.SetInt("u_tex", 0);

    // tree fragment stage is identical to grass's (kGrassFS reused as-is)
    if (!loadStage(m_tree, gl::PipelineStage::VERTEX, kTreeVS) ||
        !loadStage(m_tree, gl::PipelineStage::FRAGMENT, kGrassFS) || !m_tree.Link())
        return false;
    m_locTreeViewProj = m_tree.GetLocation("u_viewProj");
    m_tree.Bind();
    m_tree.SetInt("u_tex", 0);

    // terrain splatting shader
    if (!loadStage(m_terrainShader, gl::PipelineStage::VERTEX, kTerrainVS) ||
        !loadStage(m_terrainShader, gl::PipelineStage::FRAGMENT, kTerrainFS) ||
        !m_terrainShader.Link())
        return false;
    m_locTModel = m_terrainShader.GetLocation("u_model");
    m_locTViewProj = m_terrainShader.GetLocation("u_viewProj");
    m_locTView = m_terrainShader.GetLocation("u_view");
    m_locTLightDir = m_terrainShader.GetLocation("u_lightDir");
    m_locTAmbient = m_terrainShader.GetLocation("u_ambient");
    m_locTClipPlane = m_terrainShader.GetLocation("u_clipPlane");
    m_terrainShader.Bind();
    m_terrainShader.SetInt("u_blendMap0", 0);
    m_terrainShader.SetInt("u_blendMap1", 1);
    for (int i = 0; i < 8; ++i)
    {
        char name[32];
        snprintf(name, sizeof(name), "u_layerDiffuse[%d]", i);
        m_terrainShader.SetInt(name, 2 + i);
    }
    m_terrainShader.SetInt("u_layerCount", 0);

    // skinned forward: kSkinnedVS + the SAME forward FS = full lighting
    if (!loadStage(m_skinned, gl::PipelineStage::VERTEX, kSkinnedVS) ||
        !loadStage(m_skinned, gl::PipelineStage::FRAGMENT, kFwdFS) || !m_skinned.Link())
        return false;
    m_locSkModel = m_skinned.GetLocation("u_model");
    m_locSkViewProj = m_skinned.GetLocation("u_viewProj");
    m_locSkView = m_skinned.GetLocation("u_view");
    m_locSkClipPlane = m_skinned.GetLocation("u_clipPlane");
    m_locSkBones0 = m_skinned.GetLocation("u_bones[0]");
    m_locSkColor = m_skinned.GetLocation("u_baseColor");
    m_locSkLightDir = m_skinned.GetLocation("u_lightDir");
    m_locSkAmbient = m_skinned.GetLocation("u_ambient");
    m_locSkCameraPos = m_skinned.GetLocation("u_cameraPos");
    m_locSkSpecular = m_skinned.GetLocation("u_specular");
    m_locSkCascadeMat0 = m_skinned.GetLocation("u_lightViewProj[0]");
    m_locSkSplits0 = m_skinned.GetLocation("u_splits[0]");
    m_locSkCascadeCount = m_skinned.GetLocation("u_cascadeCount");
    m_skinned.Bind();
    m_skinned.SetInt("u_diffuse", 0);
    m_skinned.SetInt("u_shadowMap", 1);
    m_skinned.SetInt("u_detail", 6);
    m_skinned.SetInt("u_pointShadow0", 2);
    m_skinned.SetInt("u_pointShadow1", 3);
    m_skinned.SetInt("u_spotShadow0", 4);
    m_skinned.SetInt("u_spotShadow1", 5);
    m_skinned.SetInt(m_locSkCascadeCount, 0);
    m_skinned.SetInt("u_pointCount", 0); // local lights on skinned: later
    m_skinned.SetInt("u_spotCount", 0);
    m_skinned.SetFloat("u_unlit", 0.f);
    m_skinned.SetFloat("u_detailScale", 1.f);
    m_skinned.SetVec2("u_shadowMapSize", 2048.f, 2048.f);

    if (!loadStage(m_skinnedDepth, gl::PipelineStage::VERTEX, kSkinnedDepthVS) ||
        !loadStage(m_skinnedDepth, gl::PipelineStage::FRAGMENT, kDepthFS) || !m_skinnedDepth.Link())
        return false;
    m_locSkDepthMVP = m_skinnedDepth.GetLocation("u_lightMVP");
    m_locSkDepthBones0 = m_skinnedDepth.GetLocation("u_bones[0]");
    m_terrainShader.SetInt("u_shadowMap", 10); // CSM array, past the layer units
    m_terrainShader.SetInt("u_cascadeCount", 0);

    // ocean pass is optional: a compile failure only disables OceanNodes
    m_ocean_ready = loadStage(m_oceanShader, gl::PipelineStage::VERTEX, kOceanVS) &&
                    loadStage(m_oceanShader, gl::PipelineStage::FRAGMENT, kOceanFS) &&
                    m_oceanShader.Link();
    if (m_ocean_ready)
    {
        m_oceanShader.Bind();
        m_oceanShader.SetInt("reflectionTexture", 0);
        m_oceanShader.SetInt("refractionTexture", 1);
        m_oceanShader.SetInt("refractionDepth", 2);
        m_oceanShader.SetInt("waterBump", 3);
        m_oceanShader.SetInt("foamTexture", 4);
    }

    const gl::u8 white[4] = {255, 255, 255, 255};
    m_white.Load2D(white, 1, 1, gl::TextureFormat::RGBA8);
    const gl::u8 gray[4] = {128, 128, 128, 255}; // neutral for the detail multiply
    m_gray.Load2D(gray, 1, 1, gl::TextureFormat::RGBA8);
    const gl::u8 bspNeutral[4] = {43, 43, 43, 255}; // ~1/6*255 — see m_bspNeutralLM's comment
    m_bspNeutralLM.Load2D(bspNeutral, 1, 1, gl::TextureFormat::RGBA8);

    m_items.reserve(256);
    m_ready = true;
    return true;
}

void SceneRenderer::release()
{
    m_forward.Release();
    m_water.Release();
    m_mirror.Release();
    m_oceanShader.Release();
    m_sky.Release();
    m_skyboxShader.Release();
    m_skydomeShader.Release();
    m_particle.Release();
    m_grass.Release();
    m_tree.Release();
    m_terrainShader.Release();
    m_skinned.Release();
    m_skinnedDepth.Release();
    m_tonemap.Release();
    m_godray.Release();
    m_ssao.Release();
    m_ssaoBlur.Release();
    if (m_statsBatchReady) m_statsBatch.Release();
    m_statsBatchReady = false;
    m_hdrFbo.Release();
    m_pingFbo.Release();
    m_hdrColor.Release();
    m_hdrDepth.Release();
    m_hdrNormal.Release();
    m_pingColor.Release();
    m_ssaoFbo.Release();
    m_ssaoColor.Release();
    m_ssaoNoise.Release();
    m_bloomExtract.Release();
    m_bloomDownsample.Release();
    m_bloomBlur.Release();
    m_bloomUpsample.Release();
    m_bloomComposite.Release();
    for (int i = 0; i < kBloomMips; ++i)
    {
        m_bloomFbo[i].Release();
        m_bloomBlurFbo[i].Release();
        m_bloomColor[i].Release();
        m_bloomBlurColor[i].Release();
        m_bloomMipW[i] = m_bloomMipH[i] = 0;
    }
    m_postW = m_postH = 0;
    m_post_enabled = false;
    m_debug.Release();
    m_depth.Release();
    m_pointDepth.Release();
    m_shadowTex.Release();
    m_shadowFbo.Release();
    m_white.Release();
    m_gray.Release();
    m_bspNeutralLM.Release();
    m_cascades = 0;
    m_ready = false;
}

bool SceneRenderer::enable_shadows(int cascades, int resolution, float distance)
{
    if (!m_ready) return false;
    if (cascades < 1) cascades = 1;
    if (cascades > 4) cascades = 4;

    if (!loadStage(m_depth, gl::PipelineStage::VERTEX, kDepthVS) ||
        !loadStage(m_depth, gl::PipelineStage::FRAGMENT, kDepthFS) || !m_depth.Link())
        return false;
    m_locDepthMVP = m_depth.GetLocation("u_lightMVP");

    m_shadowTex.LoadDepthArray(resolution, resolution, cascades);
    m_shadowTex.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_shadowFbo.AttachTextureLayer(m_shadowTex, gl::Attachment::DEPTH, 0);
    m_shadowFbo.SetDrawBuffers();
    if (!m_shadowFbo.IsComplete()) return false;

    m_cascades = cascades;
    m_shadowSize = resolution;
    m_shadow_distance = distance;
    m_shadow_items.reserve(256);
    return true;
}

bool SceneRenderer::enable_post(bool godrays, bool ssao, bool bloom)
{
    if (!m_ready) return false;

    if (!m_tonemap.LoadFromString(gl::PipelineStage::VERTEX,
                                  gl::Renderer::FullscreenTriangleShaderSource()) ||
        !loadStage(m_tonemap, gl::PipelineStage::FRAGMENT, kTonemapFS) || !m_tonemap.Link())
        return false;
    m_tonemap.Bind();
    m_tonemap.SetInt("u_hdr", 0);

    if (godrays)
    {
        if (!m_godray.LoadFromString(gl::PipelineStage::VERTEX,
                                     gl::Renderer::FullscreenTriangleShaderSource()) ||
            !loadStage(m_godray, gl::PipelineStage::FRAGMENT, kGodrayFS) || !m_godray.Link())
            return false;
        m_godray.Bind();
        m_godray.SetInt("u_hdr", 0);
        m_godray.SetInt("u_depth", 1);
        m_godray.SetInt("u_shadowMap", 2);
    }

    if (ssao)
    {
        if (!m_ssao.LoadFromString(gl::PipelineStage::VERTEX,
                                   gl::Renderer::FullscreenTriangleShaderSource()) ||
            !loadStage(m_ssao, gl::PipelineStage::FRAGMENT, kSSAO_FS) || !m_ssao.Link())
            return false;
        if (!m_ssaoBlur.LoadFromString(gl::PipelineStage::VERTEX,
                                       gl::Renderer::FullscreenTriangleShaderSource()) ||
            !loadStage(m_ssaoBlur, gl::PipelineStage::FRAGMENT, kSSAOBlurFS) ||
            !m_ssaoBlur.Link())
            return false;

        m_ssao.Bind();
        m_ssao.SetInt("u_depth", 0);
        m_ssao.SetInt("u_noise", 1);
        m_ssao.SetInt("u_gnormal", 2);
        // hemisphere kernel, samples biased toward the center (more detail
        // close to the surface) — set once, the shader keeps it between binds
        for (int i = 0; i < 24; ++i)
        {
            Vec3 s((float)rand() / (float)RAND_MAX * 2.f - 1.f,
                   (float)rand() / (float)RAND_MAX * 2.f - 1.f, (float)rand() / (float)RAND_MAX);
            s = s.normalized() * ((float)rand() / (float)RAND_MAX);
            float scale = (float)i / 24.f;
            scale = 0.1f + 0.9f * scale * scale;
            s = s * scale;
            char name[16];
            snprintf(name, sizeof(name), "u_kernel[%d]", i);
            m_ssao.SetVec3(name, s.x, s.y, s.z);
        }

        // 8x8 tiling rotation noise — randomizes kernel orientation per
        // pixel so undersampling shows as noise (blurred away next pass)
        // instead of banding. 4x4 (16 unique rotations) was too coarse: on
        // a smooth, gently-curved surface seen at a distance/grazing angle
        // (a terrain slope, say) the tile repeat became a visible diagonal
        // banding pattern that survived the matching 4x4 blur.
        gl::u8 noiseData[8 * 8 * 2];
        for (int i = 0; i < 64; ++i)
        {
            float nx = (float)rand() / (float)RAND_MAX * 2.f - 1.f;
            float ny = (float)rand() / (float)RAND_MAX * 2.f - 1.f;
            noiseData[i * 2 + 0] = (gl::u8)((nx * 0.5f + 0.5f) * 255.f);
            noiseData[i * 2 + 1] = (gl::u8)((ny * 0.5f + 0.5f) * 255.f);
        }
        m_ssaoNoise.Load2D(noiseData, 8, 8, gl::TextureFormat::RG8);
        m_ssaoNoise.SetFilter(gl::TextureFilter::NEAREST, gl::TextureFilter::NEAREST);
        m_ssaoNoise.SetWrap(gl::TextureWrap::REPEAT, gl::TextureWrap::REPEAT);

        m_ssaoBlur.Bind();
        m_ssaoBlur.SetInt("u_ssao", 0);
        m_ssaoBlur.SetInt("u_color", 1);
    }

    if (bloom)
    {
        if (!m_bloomExtract.LoadFromString(gl::PipelineStage::VERTEX,
                                           gl::Renderer::FullscreenTriangleShaderSource()) ||
            !loadStage(m_bloomExtract, gl::PipelineStage::FRAGMENT, kBloomExtractFS) ||
            !m_bloomExtract.Link())
            return false;
        m_bloomExtract.Bind();
        m_bloomExtract.SetInt("u_hdr", 0);
        m_bloomExtract.SetFloat("u_exposure", 1.0f);

        if (!m_bloomBlur.LoadFromString(gl::PipelineStage::VERTEX,
                                        gl::Renderer::FullscreenTriangleShaderSource()) ||
            !loadStage(m_bloomBlur, gl::PipelineStage::FRAGMENT, kBloomBlurFS) ||
            !m_bloomBlur.Link())
            return false;
        m_bloomBlur.Bind();
        m_bloomBlur.SetInt("u_tex", 0);

        if (!m_bloomDownsample.LoadFromString(gl::PipelineStage::VERTEX,
                                              gl::Renderer::FullscreenTriangleShaderSource()) ||
            !loadStage(m_bloomDownsample, gl::PipelineStage::FRAGMENT, kBloomDownsampleFS) ||
            !m_bloomDownsample.Link())
            return false;
        m_bloomDownsample.Bind();
        m_bloomDownsample.SetInt("u_src", 0);

        if (!m_bloomUpsample.LoadFromString(gl::PipelineStage::VERTEX,
                                            gl::Renderer::FullscreenTriangleShaderSource()) ||
            !loadStage(m_bloomUpsample, gl::PipelineStage::FRAGMENT, kBloomUpsampleFS) ||
            !m_bloomUpsample.Link())
            return false;
        m_bloomUpsample.Bind();
        m_bloomUpsample.SetInt("u_big", 0);
        m_bloomUpsample.SetInt("u_small", 1);

        if (!m_bloomComposite.LoadFromString(gl::PipelineStage::VERTEX,
                                             gl::Renderer::FullscreenTriangleShaderSource()) ||
            !loadStage(m_bloomComposite, gl::PipelineStage::FRAGMENT, kBloomCompositeFS) ||
            !m_bloomComposite.Link())
            return false;
        m_bloomComposite.Bind();
        m_bloomComposite.SetInt("u_color", 0);
        m_bloomComposite.SetInt("u_bloom", 1);
    }

    m_post_enabled = true;
    m_godrays_enabled = godrays;
    m_ssao_enabled = ssao;
    m_bloom_enabled = bloom;
    return true;
}

// (re)creates the HDR targets when the viewport size changes
bool SceneRenderer::ensure_post_targets(int w, int h)
{
    if (m_postW == w && m_postH == h) return true;

    m_hdrColor.Release();
    m_hdrDepth.Release();
    m_hdrNormal.Release();
    m_pingColor.Release();
    m_hdrFbo.Release();
    m_pingFbo.Release();
    m_ssaoColor.Release();
    m_ssaoFbo.Release();

    m_hdrColor.Load2D(nullptr, w, h, gl::TextureFormat::RGBA16F);
    m_hdrColor.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    m_hdrColor.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_hdrNormal.Load2D(nullptr, w, h, gl::TextureFormat::RGB16F);
    m_hdrNormal.SetFilter(gl::TextureFilter::NEAREST, gl::TextureFilter::NEAREST);
    m_hdrNormal.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_hdrDepth.LoadDepth(w, h, gl::TextureFormat::DEPTH24);
    m_hdrFbo.AttachTexture(m_hdrColor, gl::Attachment::COLOR0);
    m_hdrFbo.AttachTexture(m_hdrNormal, gl::Attachment::COLOR1);
    m_hdrFbo.AttachTexture(m_hdrDepth, gl::Attachment::DEPTH);
    m_hdrFbo.SetDrawBuffers();
    if (!m_hdrFbo.IsComplete()) return false;

    m_pingColor.Load2D(nullptr, w, h, gl::TextureFormat::RGBA16F);
    m_pingColor.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    m_pingColor.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_pingFbo.AttachTexture(m_pingColor, gl::Attachment::COLOR0);
    m_pingFbo.SetDrawBuffers();
    if (!m_pingFbo.IsComplete()) return false;

    m_ssaoColor.Load2D(nullptr, w, h, gl::TextureFormat::R8);
    m_ssaoColor.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    m_ssaoColor.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_ssaoFbo.AttachTexture(m_ssaoColor, gl::Attachment::COLOR0);
    m_ssaoFbo.SetDrawBuffers();
    if (!m_ssaoFbo.IsComplete()) return false;

    int mw = w, mh = h;
    for (int i = 0; i < kBloomMips; ++i)
    {
        mw = mw / 2 < 1 ? 1 : mw / 2;
        mh = mh / 2 < 1 ? 1 : mh / 2;

        m_bloomColor[i].Release();
        m_bloomFbo[i].Release();
        m_bloomColor[i].Load2D(nullptr, mw, mh, gl::TextureFormat::RGBA16F);
        m_bloomColor[i].SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
        m_bloomColor[i].SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
        m_bloomFbo[i].AttachTexture(m_bloomColor[i], gl::Attachment::COLOR0);
        m_bloomFbo[i].SetDrawBuffers();
        if (!m_bloomFbo[i].IsComplete()) return false;

        m_bloomBlurColor[i].Release();
        m_bloomBlurFbo[i].Release();
        m_bloomBlurColor[i].Load2D(nullptr, mw, mh, gl::TextureFormat::RGBA16F);
        m_bloomBlurColor[i].SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
        m_bloomBlurColor[i].SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
        m_bloomBlurFbo[i].AttachTexture(m_bloomBlurColor[i], gl::Attachment::COLOR0);
        m_bloomBlurFbo[i].SetDrawBuffers();
        if (!m_bloomBlurFbo[i].IsComplete()) return false;

        m_bloomMipW[i] = mw;
        m_bloomMipH[i] = mh;
    }

    m_postW = w;
    m_postH = h;
    return true;
}

void SceneRenderer::draw_shadow_views(Scene& scene, Camera3D* camera)
{
    float farClip = camera->get_far();
    if (farClip > m_shadow_distance) farClip = m_shadow_distance;
    csmSplits(camera->get_near(), farClip, m_splits, m_cascades);

    Mat4 view = camera->get_view_matrix();
    for (int c = 0; c < m_cascades; ++c)
    {
        float texelWorldSize;
        m_cascadeMat[c] = csmCascadeMatrix(c, camera->get_aspect(), camera->get_fov(), view,
                                           m_splits, m_lightDir, (float)m_shadowSize,
                                           m_shadow_distance, texelWorldSize);
    }

    m_shadowFbo.Bind();
    gl::Renderer::Viewport(0, 0, m_shadowSize, m_shadowSize);
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetPolygonOffset(true, m_shadowPolygonOffsetFactor, m_shadowPolygonOffsetUnits);

    m_depth.Bind();
    for (int c = 0; c < m_cascades; ++c)
    {
        // cull against THIS cascade's own light-space frustum — it was
        // already extended (csmCascadeMatrix's depthScale) to catch casters
        // outside the camera view, so this doesn't drop legitimate casters;
        // it drops the rest of the scene, which used to be drawn into every
        // cascade layer regardless of distance (a terrain-sized scene paid
        // for the whole heightmap 4x over, unculled, every frame)
        Frustum cascadeFrustum;
        cascadeFrustum.build(Mat4::Identity(), m_cascadeMat[c]);
        m_shadow_items.clear();
        scene.collect(m_shadow_items, &cascadeFrustum, (m_octree_built && m_octree_enabled) ? &m_octree : nullptr);

        m_shadowFbo.AttachTextureLayer(m_shadowTex, gl::Attachment::DEPTH, c);
        gl::Renderer::Clear(false, true);
        for (const RenderItem& item : m_shadow_items)
        {
            Mat4 mvp = m_cascadeMat[c] * item.world;
            m_depth.SetMat4(m_locDepthMVP, mvp.x);
            item.vao->Bind();
            gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, item.index_count,
                                      item.first_index);
        }
        // splat-mode terrain pages cast too (they aren't RenderItems);
        // culled against the same cascade frustum, at their current LOD
        for (TerrainPagingNode* t : m_pagedTerrains)
            t->render_pages_depth(&m_depth, m_locDepthMVP, m_cascadeMat[c], &cascadeFrustum);

        // skinned characters cast with their current pose
        if (!m_skinnedMeshes.empty())
        {
            m_skinnedDepth.Bind();
            for (SkinnedMeshInstance* sk : m_skinnedMeshes)
            {
                SkinnedMesh* res = sk->get_mesh();
                if (!res || !res->ensure_gpu()) continue;
                const std::vector<Mat4>& palette = sk->palette();
                const int nBones = (int)palette.size() > 80 ? 80 : (int)palette.size();
                for (int i = 0; i < nBones; ++i)
                    m_skinnedDepth.SetMat4(m_locSkDepthBones0 + i, palette[(size_t)i].x);
                Mat4 mvp = m_cascadeMat[c] * sk->get_global_transform();
                m_skinnedDepth.SetMat4(m_locSkDepthMVP, mvp.x);
                Mesh& mesh = res->mesh();
                mesh.vao().Bind();
                for (const Surface& surf : mesh.surfaces())
                    gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, surf.index_count,
                                              surf.first_index);
            }
            m_depth.Bind(); // next cascade continues with the static depth shader
        }
    }
    gl::Renderer::SetPolygonOffset(false);
    gl::Renderer::SetCull(gl::CullMode::BACK);
}

void SceneRenderer::set_clear_color(float r, float g, float b)
{
    m_clearColor = Vec3(r, g, b);
}

void SceneRenderer::set_light_dir(const Vec3& dir)
{
    m_lightDir = dir.normalized();
}

void SceneRenderer::collect_lights(Node* node, std::vector<LightNode*>& out)
{
    LightNode* l = node->as<LightNode>();
    if (l) out.push_back(l);
    for (Node* child : node->get_children())
        collect_lights(child, out);
}

// Renders the shadow maps of shadow-casting local lights: six distance-cube
// faces per point light, one projective map per spot. The first two
// shadow-casting lights of each type get a slot; the rest light without
// shadows.
void SceneRenderer::draw_light_shadows(Scene& scene)
{
 

    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetDepthFunction(gl::DepthFunction::LESS);
    gl::Renderer::SetCull(gl::CullMode::NONE);

    int pointSlot = 0, spotSlot = 0;
    for (LightNode* light : m_lights)
    {
        if (!light->cast_shadows) continue;
        if (!light->ensure_gpu()) continue;
        const Vec3 pos = light->get_global_position();
        PointLight* point = light->as<PointLight>();
        SpotLight* spot = light->as<SpotLight>();

        if (point && pointSlot < 2)
        {
            light->shadow_fbo().Bind();
            gl::Renderer::Viewport(0, 0, light->shadow_resolution, light->shadow_resolution);
            Mat4 proj = Mat4::Perspective(90.0, 1.0, 0.05, (double)point->range);

            static const Vec3 kFaceDir[6] = {Vec3(1, 0, 0),  Vec3(-1, 0, 0), Vec3(0, 1, 0),
                                             Vec3(0, -1, 0), Vec3(0, 0, 1),  Vec3(0, 0, -1)};
            static const Vec3 kFaceUp[6] = {Vec3(0, -1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1),
                                            Vec3(0, 0, -1), Vec3(0, -1, 0), Vec3(0, -1, 0)};

            m_pointDepth.Bind();
            m_pointDepth.SetVec3(m_locPDLightPos, pos.x, pos.y, pos.z);
            m_pointDepth.SetFloat(m_locPDRange, point->range);
            for (int face = 0; face < 6; ++face)
            {
                light->shadow_fbo().AttachCubeFace(light->shadow_tex(), gl::Attachment::DEPTH,
                                                   (gl::u32)face);
                gl::Renderer::Clear(false, true);
                Mat4 viewFace = Mat4::LookAt(pos, pos + kFaceDir[face], kFaceUp[face]);
                Mat4 vp = proj * viewFace;
                m_pointDepth.SetMat4(m_locPDLightVP, vp.x);

                // cull per cube face: only render surfaces within this face's
                // 90° pyramid out to the light's range
                Frustum faceFrustum;
                faceFrustum.build(viewFace, proj);
                m_shadow_items.clear();
                scene.collect(m_shadow_items, &faceFrustum, (m_octree_built && m_octree_enabled) ? &m_octree : nullptr);

                for (const RenderItem& item : m_shadow_items)
                {
                    m_pointDepth.SetMat4(m_locPDModel, item.world.x);
                    item.vao->Bind();
                    gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, item.index_count,
                                              item.first_index);
                }
            }
            ++pointSlot;
        }
        else if (spot && spotSlot < 2)
        {
            light->shadow_fbo().Bind();
            gl::Renderer::Viewport(0, 0, light->shadow_resolution, light->shadow_resolution);
            gl::Renderer::Clear(false, true);
            ////gl::Renderer::SetPolygonOffset(true, 2.f, 3.f);

            const Vec3 dir = spot->direction();
            const float fovDeg = spot->outer_angle * 2.f * 57.29578f;
            // the near plane must scale with the range: a tiny near crams
            // all usable depth into the last few thousandths of the map and
            // the compare bias then covers the whole scene (no shadow)
            const float nearPlane = spot->range * 0.05f;
            Mat4 viewSpot = Mat4::LookAt(pos, pos + dir, Vec3(0.001f, 1.f, 0.001f));
            Mat4 projSpot =
                Mat4::Perspective((double)fovDeg, 1.0, (double)nearPlane, (double)spot->range);
            Mat4 vp = projSpot * viewSpot;
            m_spotShadowMat[spotSlot] = vp;

            // cull against the spot's own frustum cone
            Frustum spotFrustum;
            spotFrustum.build(viewSpot, projSpot);
            m_shadow_items.clear();
            scene.collect(m_shadow_items, &spotFrustum, (m_octree_built && m_octree_enabled) ? &m_octree : nullptr);

            m_depth.Bind();
            for (const RenderItem& item : m_shadow_items)
            {
                Mat4 mvp = vp * item.world;
                m_depth.SetMat4(m_locDepthMVP, mvp.x);
                item.vao->Bind();
                gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, item.index_count,
                                          item.first_index);
            }
            //gl::Renderer::SetPolygonOffset(false);
            ++spotSlot;
        }
    }
}

// Feeds the collected lights to the forward shader; called once per view.
void SceneRenderer::set_light_uniforms()
{
    int points = 0, spots = 0, pointSlot = 0, spotSlot = 0;
    char name[64];
    for (LightNode* light : m_lights)
    {
        const Vec3 pos = light->get_global_position();
        const Vec3 c = light->color * light->intensity;
        PointLight* point = light->as<PointLight>();
        SpotLight* spot = light->as<SpotLight>();

        if (point && points < 4)
        {
            float slot = -1.f;
            if (point->cast_shadows && pointSlot < 2)
            {
                point->shadow_tex().Bind(2 + (gl::u32)pointSlot);
                slot = (float)pointSlot++;
            }
            std::snprintf(name, sizeof(name), "u_pointPosRange[%d]", points);
            m_forward.SetVec4(name, pos.x, pos.y, pos.z, point->range);
            std::snprintf(name, sizeof(name), "u_pointColor[%d]", points);
            m_forward.SetVec4(name, c.x, c.y, c.z, slot);
            ++points;
        }
        else if (spot && spots < 4)
        {
            float slot = -1.f;
            if (spot->cast_shadows && spotSlot < 2)
            {
                spot->shadow_tex().Bind(4 + (gl::u32)spotSlot);
                std::snprintf(name, sizeof(name), "u_spotShadowMat[%d]", spotSlot);
                m_forward.SetMat4(name, m_spotShadowMat[spotSlot].x);
                slot = (float)spotSlot++;
            }
            const Vec3 dir = spot->direction();
            std::snprintf(name, sizeof(name), "u_spotPosRange[%d]", spots);
            m_forward.SetVec4(name, pos.x, pos.y, pos.z, spot->range);
            std::snprintf(name, sizeof(name), "u_spotDirInner[%d]", spots);
            m_forward.SetVec4(name, dir.x, dir.y, dir.z, cosf(spot->inner_angle));
            std::snprintf(name, sizeof(name), "u_spotColorOuter[%d]", spots);
            m_forward.SetVec4(name, c.x, c.y, c.z, cosf(spot->outer_angle));
            std::snprintf(name, sizeof(name), "u_spotShadowSlot[%d]", spots);
            m_forward.SetFloat(name, slot);
            ++spots;
        }
    }
    m_forward.SetInt("u_pointCount", points);
    m_forward.SetInt("u_spotCount", spots);
}

void SceneRenderer::collect_water(Node* node, std::vector<WaterNode*>& out)
{
    WaterNode* w = node->as<WaterNode>();
    if (w) out.push_back(w);
    for (Node* child : node->get_children())
        collect_water(child, out);
}

void SceneRenderer::collect_mirrors(Node* node, std::vector<MirrorNode*>& out)
{
    MirrorNode* m = node->as<MirrorNode>();
    if (m) out.push_back(m);
    for (Node* child : node->get_children())
        collect_mirrors(child, out);
}

void SceneRenderer::collect_particles(Node* node, std::vector<ParticleSystemNode*>& out)
{
    ParticleSystemNode* p = node->as<ParticleSystemNode>();
    if (p) out.push_back(p);
    for (Node* child : node->get_children())
        collect_particles(child, out);
}

void SceneRenderer::collect_decals(Node* node, std::vector<DecalSystemNode*>& out)
{
    DecalSystemNode* d = node->as<DecalSystemNode>();
    if (d) out.push_back(d);
    for (Node* child : node->get_children())
        collect_decals(child, out);
}

void SceneRenderer::collect_grass(Node* node, std::vector<GrassSystemNode*>& out)
{
    GrassSystemNode* g = node->as<GrassSystemNode>();
    if (g) out.push_back(g);
    for (Node* child : node->get_children())
        collect_grass(child, out);
}

void SceneRenderer::collect_trees(Node* node, std::vector<TreeSystemNode*>& out)
{
    TreeSystemNode* t = node->as<TreeSystemNode>();
    if (t) out.push_back(t);
    for (Node* child : node->get_children())
        collect_trees(child, out);
}

void SceneRenderer::collect_ribbontrails(Node* node, std::vector<RibbonTrailNode*>& out)
{
    RibbonTrailNode* r = node->as<RibbonTrailNode>();
    if (r) out.push_back(r);
    for (Node* child : node->get_children())
        collect_ribbontrails(child, out);
}

void SceneRenderer::collect_billboards(Node* node, std::vector<BillboardNode*>& out)
{
    BillboardNode* b = node->as<BillboardNode>();
    if (b) out.push_back(b);
    for (Node* child : node->get_children())
        collect_billboards(child, out);
}

void SceneRenderer::collect_paged_terrain(Node* node, std::vector<TerrainPagingNode*>& out)
{
    TerrainPagingNode* t = node->as<TerrainPagingNode>();
    if (t && t->layer_count() > 0) out.push_back(t); // splat mode only
    for (Node* child : node->get_children())
        collect_paged_terrain(child, out);
}

// Ogre-style texture splatting over the paged terrain: base layer + up to 4
// layers mixed by each page's RGBA blend map. Runs inside draw_view, so the
// same pass serves the main view and the water reflection/refraction views
// (the vertex shader honors the view's clip plane).
void SceneRenderer::draw_paged_terrain(const RenderView& v, const Frustum& frustum)
{
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetCull(gl::CullMode::BACK);
    gl::Renderer::SetBlend(false);

    m_terrainShader.Bind();
    Mat4 vp = v.proj * v.view;
    m_terrainShader.SetMat4(m_locTViewProj, vp.x);
    m_terrainShader.SetMat4(m_locTView, v.view.x);
    m_terrainShader.SetVec3(m_locTLightDir, m_lightDir.x, m_lightDir.y, m_lightDir.z);
    m_terrainShader.SetVec3(m_locTAmbient, m_ambientColor.x, m_ambientColor.y, m_ambientColor.z);
    m_terrainShader.SetVec4(m_locTClipPlane, v.clip_plane.x, v.clip_plane.y, v.clip_plane.z,
                            v.clip_plane.w);

    // same CSM the forward pass uses; the terrain shader samples it with
    // the identical occlusion functions
    m_terrainShader.SetInt("u_cascadeCount", m_cascades);
    if (m_cascades > 0)
    {
        m_shadowTex.Bind(10);
        m_terrainShader.SetVec2("u_shadowMapSize", (float)m_shadowSize, (float)m_shadowSize);
        m_terrainShader.SetFloat("u_shadowNormalBias", m_shadowNormalBias);
        for (int i = 0; i < m_cascades; ++i)
        {
            char name[32];
            snprintf(name, sizeof(name), "u_lightViewProj[%d]", i);
            m_terrainShader.SetMat4(name, m_cascadeMat[i].x);
            snprintf(name, sizeof(name), "u_splits[%d]", i);
            m_terrainShader.SetFloat(name, m_splits[i + 1]);
        }
    }

    m_terrainShader.SetVec3("u_cameraPos", v.cam_pos.x, v.cam_pos.y, v.cam_pos.z);

    for (TerrainPagingNode* t : m_pagedTerrains)
    {
        const int n = t->layer_count();
        m_terrainShader.SetInt("u_layerCount", n);

        // per-node linear fog (end <= 0 disables in the shader). The color
        // follows the sun: full at day, warm-shifted near the horizon,
        // near-black at night — fog that stays bright after sunset glows.
        if (t->fog_enabled())
        {
            const float sunElev = -m_lightDir.y; // sin(elevation) of the sun
            float day = sunElev / 0.25f;
            day = day < 0.03f ? 0.03f : (day > 1.f ? 1.f : day);
            // low sun keeps red longest (same idea as the sky's sunset tint)
            float warm = 1.f - day;
            Vec3 fc = t->fog_color();
            fc = Vec3(fc.x * day * (1.f + 0.35f * warm), fc.y * day * (1.f + 0.10f * warm),
                      fc.z * day);
            m_terrainShader.SetVec3("u_fogColor", fc.x, fc.y, fc.z);
            m_terrainShader.SetVec2("u_fogRange", t->fog_start(), t->fog_end());
        }
        else
            m_terrainShader.SetVec2("u_fogRange", 0.f, 0.f);
        for (int i = 0; i < n; ++i)
        {
            char name[32];
            snprintf(name, sizeof(name), "u_layerTiling[%d]", i);
            gl::Texture* diff = t->layer_texture(i);
            (diff ? diff : &m_white)->Bind((gl::u32)(2 + i));
            m_terrainShader.SetFloat(name, t->layer_tiling(i));
        }
        t->render_pages(&m_terrainShader, m_locTModel, &frustum);
    }

    m_forward.Bind(); // draw_view continues with the forward shader state
}

void SceneRenderer::collect_skinned(Node* node, std::vector<SkinnedMeshInstance*>& out)
{
    SkinnedMeshInstance* s = node->as<SkinnedMeshInstance>();
    if (s && s->get_mesh()) out.push_back(s);
    for (Node* child : node->get_children())
        collect_skinned(child, out);
}

// GPU skinning: one draw per instance, palette in u_bones[]. Linked against
// the forward FS, so sun light, CSM shadows and specular all apply.
void SceneRenderer::draw_skinned(const RenderView& v, const Frustum& frustum)
{
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetCull(gl::CullMode::BACK);
    gl::Renderer::SetBlend(false);

    m_skinned.Bind();
    Mat4 vp = v.proj * v.view;
    m_skinned.SetMat4(m_locSkViewProj, vp.x);
    m_skinned.SetMat4(m_locSkView, v.view.x);
    m_skinned.SetVec3(m_locSkLightDir, m_lightDir.x, m_lightDir.y, m_lightDir.z);
    m_skinned.SetVec3(m_locSkAmbient, m_ambientColor.x, m_ambientColor.y, m_ambientColor.z);
    m_skinned.SetVec3(m_locSkCameraPos, v.cam_pos.x, v.cam_pos.y, v.cam_pos.z);
    m_skinned.SetVec4(m_locSkClipPlane, v.clip_plane.x, v.clip_plane.y, v.clip_plane.z,
                      v.clip_plane.w);
    m_skinned.SetInt(m_locSkCascadeCount, m_cascades);
    if (m_cascades > 0)
    {
        m_shadowTex.Bind(1);
        m_skinned.SetVec2("u_shadowMapSize", (float)m_shadowSize, (float)m_shadowSize);
        m_skinned.SetFloat("u_shadowNormalBias", m_shadowNormalBias);
        for (int i = 0; i < m_cascades; ++i)
        {
            m_skinned.SetMat4(m_locSkCascadeMat0 + i, m_cascadeMat[i].x);
            m_skinned.SetFloat(m_locSkSplits0 + i, m_splits[i + 1]);
        }
    }

    for (SkinnedMeshInstance* s : m_skinnedMeshes)
    {
        SkinnedMesh* res = s->get_mesh();
        if (!res || !res->ensure_gpu()) continue;

        const Mat4 world = s->get_global_transform();
        Mesh& mesh = res->mesh();
        if (!mesh.surfaces().empty())
        {
            // whole-character cull with the first surface's bounds
            BoundingBox bb = BoundingBox::TransformBoundingBox(mesh.surfaces()[0].bounds, world);
            for (size_t i = 1; i < mesh.surfaces().size(); ++i)
                bb.Merge(BoundingBox::TransformBoundingBox(mesh.surfaces()[i].bounds, world));
            if (!frustum.ContainsBox(bb)) continue;
        }

        const std::vector<Mat4>& palette = s->palette();
        const int nBones = (int)palette.size() > 80 ? 80 : (int)palette.size();
        for (int i = 0; i < nBones; ++i)
            m_skinned.SetMat4(m_locSkBones0 + i, palette[(size_t)i].x);

        m_skinned.SetMat4(m_locSkModel, world.x);
        mesh.vao().Bind();
        m_gray.Bind(6);

        const std::vector<Material*>& mats = s->get_materials();
        for (const Surface& surf : mesh.surfaces())
        {
            const Material* mat = (surf.material_slot >= 0 && surf.material_slot < (int)mats.size())
                                      ? mats[surf.material_slot]
                                      : (!mats.empty() ? mats[0] : nullptr);
            Vec3 color = mat ? mat->base_color : Vec3(1.f, 1.f, 1.f);
            gl::Texture* diffuse = (mat && mat->diffuse) ? mat->diffuse : &m_white;
            diffuse->Bind(0);
            m_skinned.SetVec3(m_locSkColor, color.x, color.y, color.z);
            m_skinned.SetVec2(m_locSkSpecular, mat ? mat->specular : 0.f, mat ? mat->shininess : 32.f);
            gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, surf.index_count,
                                      surf.first_index);
        }
    }

    m_forward.Bind(); // draw_view continues with the forward shader
}

// Alpha-cutout, not alpha-blend: depth test+write ON, no cull (blades are
// single-sided planes meant to be seen from both faces), no blending or
// sorting needed since a fragment is either fully there or fully gone.
void SceneRenderer::draw_grass(const Mat4& viewProj)
{
    if (m_grassSystems.empty()) return;

    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(false);

    m_grass.Bind();
    m_grass.SetMat4(m_locGViewProj, viewProj.x);

    for (GrassSystemNode* g : m_grassSystems)
    {
        if (g->index_count() == 0) continue;
        gl::Texture* tex = g->texture ? g->texture : &m_white;
        tex->Bind(0);
        m_grass.SetFloat("u_time", g->time());
        m_grass.SetVec3("u_windDir", g->wind_dir().x, g->wind_dir().y, g->wind_dir().z);
        m_grass.SetFloat("u_windStrength", g->wind_strength());
        m_grass.SetFloat("u_windSpeed", g->wind_speed());
        m_grass.SetFloat("u_cutout", g->cutout());
        m_grass.SetVec3("u_lightDir", g->light_dir().x, g->light_dir().y, g->light_dir().z);
        m_grass.SetVec3("u_lightColor", g->light_color().x, g->light_color().y, g->light_color().z);
        m_grass.SetFloat("u_ambient", g->ambient());
        g->mesh().vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, g->index_count());
    }
}

// same state/technique as draw_grass — see its own comment
void SceneRenderer::draw_trees(const Mat4& viewProj)
{
    if (m_treeSystems.empty()) return;

    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(false);

    m_tree.Bind();
    m_tree.SetMat4(m_locTreeViewProj, viewProj.x);

    for (TreeSystemNode* t : m_treeSystems)
    {
        if (t->index_count() == 0) continue;
        gl::Texture* tex = t->texture ? t->texture : &m_white;
        tex->Bind(0);
        m_tree.SetFloat("u_time", t->time());
        m_tree.SetVec3("u_windDir", t->wind_dir().x, t->wind_dir().y, t->wind_dir().z);
        m_tree.SetFloat("u_windStrength", t->wind_strength());
        m_tree.SetFloat("u_windSpeed", t->wind_speed());
        m_tree.SetFloat("u_cutout", t->cutout());
        m_tree.SetVec3("u_lightDir", t->light_dir().x, t->light_dir().y, t->light_dir().z);
        m_tree.SetVec3("u_lightColor", t->light_color().x, t->light_color().y, t->light_color().z);
        m_tree.SetFloat("u_ambient", t->ambient());
        t->mesh().vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, t->index_count());
    }
}

// Billboards are baked in world space by each node's simulation step
// (Scene::update -> Node3D::_update -> simulate); here we only need the
// camera basis to orient them and a viewProj to draw. Blend mode is
// per-system (additive glow vs. alpha smoke); depth write off so
// overlapping particles don't fight each other, depth test on so they sink
// behind opaque geometry correctly.
void SceneRenderer::draw_particles(const Mat4& viewProj)
{
    if (m_particleSystems.empty() && m_decalSystems.empty()) return;

    gl::Renderer::SetDepthWrite(false);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(true);

    m_particle.Bind();
    m_particle.SetMat4(m_locPViewProj, viewProj.x);

    for (ParticleSystemNode* ps : m_particleSystems)
    {
        if (ps->active_count() <= 0) continue;
        if (ps->blend == ParticleBlendMode::Additive)
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE);
        else
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA,
                                          gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
        gl::Texture* tex = ps->texture ? ps->texture : &m_white;
        tex->Bind(0);
        ps->quad_mesh().vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, (u32)ps->active_count() * 6u);
    }

    // decals sit exactly on the surface they mark — a small offset keeps
    // them from z-fighting with it (same technique the shadow pass uses
    // for coplanar geometry)
    if (!m_decalSystems.empty()) gl::Renderer::SetPolygonOffset(true, -1.f, -2.f);
    for (DecalSystemNode* ds : m_decalSystems)
    {
        if (ds->active_count() <= 0) continue;
        if (ds->blend == ParticleBlendMode::Additive)
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE);
        else
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA,
                                          gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
        gl::Texture* tex = ds->texture ? ds->texture : &m_white;
        tex->Bind(0);
        ds->quad_mesh().vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, (u32)ds->active_count() * 6u);
    }
    if (!m_decalSystems.empty()) gl::Renderer::SetPolygonOffset(false);

    gl::Renderer::SetBlend(false);
    gl::Renderer::SetDepthWrite(true);
}

// Ribbon trails reuse the particle shader (same vertex layout:
// world-space position, color in tangent, uv). Each trail bakes a
// dynamic triangle strip during rebuild() and is drawn with its own
// texture/blend mode, depth-write off for proper alpha layering.
void SceneRenderer::draw_ribbontrails(const Mat4& viewProj)
{
    if (m_ribbonTrails.empty()) return;

    gl::Renderer::SetDepthWrite(false);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(true);

    m_particle.Bind();
    m_particle.SetMat4(m_locPViewProj, viewProj.x);

    for (RibbonTrailNode* rt : m_ribbonTrails)
    {
        if (rt->index_count() == 0) continue;
        gl::Renderer::SetDepthTest(rt->depthTest);
        if (rt->blend == ParticleBlendMode::Additive)
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE);
        else
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA,
                                          gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
        gl::Texture* tex = rt->texture ? rt->texture : &m_white;
        tex->Bind(0);
        rt->quad_mesh().vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, rt->index_count());
    }

    gl::Renderer::SetBlend(false);
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
}

// Billboards reuse the particle shader too (same vertex layout). Each one
// is a single quad rebuilt every frame in rebuild() with its own
// view-mode/atlas-rect choice, drawn with its own texture/blend/depth-test.
void SceneRenderer::draw_billboards(const Mat4& viewProj)
{
    if (m_billboards.empty()) return;

    gl::Renderer::SetDepthWrite(false);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(true);

    m_particle.Bind();
    m_particle.SetMat4(m_locPViewProj, viewProj.x);

    for (BillboardNode* b : m_billboards)
    {
        gl::Renderer::SetDepthTest(b->depthTest);
        if (b->blend == ParticleBlendMode::Additive)
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE);
        else
            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA,
                                          gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
        gl::Texture* tex = b->texture ? b->texture : &m_white;
        tex->Bind(0);
        b->quad_mesh().vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, 6);
    }

    gl::Renderer::SetBlend(false);
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
}

// Lens flare: screen-space quads along the sun→center axis, additive
// blend, depth off. Each LensFlareNode bakes NDC geometry; here we
// iterate all nodes in the scene and draw them.
void SceneRenderer::draw_lensflares(Camera3D& cam)
{
    for (LensFlareNode* node : m_lensflares)
    {
        if (!node->is_enabled()) continue;
        if (!node->ensure_gpu()) continue;

        u32 count = node->build(cam, -m_lightDir);
        if (count == 0) continue;

        gl::Shader* sh = node->shader();
        if (!sh) continue;

        gl::Renderer::SetDepthTest(false);
        gl::Renderer::SetDepthWrite(false);
        gl::Renderer::SetCull(gl::CullMode::NONE);
        gl::Renderer::SetBlend(true);
        gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE);

        sh->Bind();
        sh->SetInt("u_tex", 0);
        node->tex().Bind(0);

        node->vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, count);

        gl::Renderer::SetBlend(false);
        gl::Renderer::SetDepthTest(true);
        gl::Renderer::SetDepthWrite(true);
    }
}

void SceneRenderer::collect_lensflares(Node* node, std::vector<LensFlareNode*>& out)
{
    LensFlareNode* l = node->as<LensFlareNode>();
    if (l) out.push_back(l);
    for (Node* child : node->get_children())
        collect_lensflares(child, out);
}

void SceneRenderer::draw_view(Scene& scene, const RenderView& v)
{
    // cull against this view's own frustum
    Frustum frustum;
    frustum.build(v.view, v.proj);
    m_items.clear();
    auto collectT0 = std::chrono::steady_clock::now();
    scene.collect(m_items, &frustum, (m_octree_built && m_octree_enabled) ? &m_octree : nullptr);
    auto collectT1 = std::chrono::steady_clock::now();
    m_lastCollectMs = std::chrono::duration<double, std::milli>(collectT1 - collectT0).count();
    m_bspInstances.clear();
    scene.collect_bsp(m_bspInstances);
    m_bspEntityInstances.clear();
    scene.collect_bsp_entities(m_bspEntityInstances);
    m_last_items = (int)m_items.size() + (int)m_bspInstances.size() + (int)m_bspEntityInstances.size();

    if (v.target)
    {
        v.target->Bind();
        gl::Renderer::Viewport(0, 0, v.w, v.h);
    }
    else
    {
        // the real screen framebuffer — offset+scaled by render_fit()'s
        // letterbox/crop rect if it's in effect (m_outputX/Y are 0,0 and
        // m_outputW/H are 0 for plain render(), which falls back to v.w/h)
        gl::Renderer::BindScreen();
        gl::Renderer::Viewport(m_outputX, m_outputY, m_outputW > 0 ? m_outputW : v.w,
                               m_outputH > 0 ? m_outputH : v.h);
    }
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetCull(gl::CullMode::BACK);
    // a mirrored view reverses triangle winding: flip what counts as front
    gl::Renderer::SetFrontFaceCW(v.mirrored);
    gl::Renderer::SetClipDistance(0, v.use_clip);
    gl::Renderer::ClearColor(m_clearColor.x, m_clearColor.y, m_clearColor.z, 1.0f);
    gl::Renderer::Clear(true, true);
    // normal G-buffer (COLOR1): zero is the "nothing wrote here" sentinel
    // kSSAO_FS falls back on — only exists on the main HDR target, not the
    // water/mirror reflection FBOs, which don't attach it.
    if (v.target == &m_hdrFbo) gl::Renderer::ClearColorAttachment(1, 0.f, 0.f, 0.f, 0.f);

    m_forward.Bind();
    Mat4 vp = v.proj * v.view;
    m_forward.SetMat4(m_locViewProj, vp.x);
    m_forward.SetMat4(m_locView, v.view.x);
    m_forward.SetVec3(m_locLightDir, m_lightDir.x, m_lightDir.y, m_lightDir.z);
    m_forward.SetVec3(m_locAmbient, m_ambientColor.x, m_ambientColor.y, m_ambientColor.z);
    m_forward.SetVec3(m_locCameraPos, v.cam_pos.x, v.cam_pos.y, v.cam_pos.z);
    m_forward.SetVec4(m_locClipPlane, v.clip_plane.x, v.clip_plane.y, v.clip_plane.z,
                      v.clip_plane.w);

    set_light_uniforms();

    // BSP per-view uniforms (before the forward pass binds its own shader)
    m_bsp.Bind();
    m_bsp.SetMat4(m_locBspViewProj, vp.x);
    m_bsp.SetMat4(m_locBspView, v.view.x);
    m_bsp.SetVec3(m_locBspLightDir, m_lightDir.x, m_lightDir.y, m_lightDir.z);
    m_bsp.SetVec3(m_locBspCameraPos, v.cam_pos.x, v.cam_pos.y, v.cam_pos.z);
    m_bsp.SetVec4(m_locBspClipPlane, v.clip_plane.x, v.clip_plane.y, v.clip_plane.z,
                  v.clip_plane.w);

    m_forward.Bind();
    m_forward.SetInt(m_locCascadeCount, m_shadows_active ? m_cascades : 0);
    if (m_shadows_active && m_cascades > 0)
    {
        m_shadowTex.Bind(1);
        m_forward.SetInt(m_locShowCascades, m_show_cascades ? 1 : 0);
        m_forward.SetVec2(m_locShadowSize, (float)m_shadowSize, (float)m_shadowSize);
        m_forward.SetFloat(m_locShadowNormalBias, m_shadowNormalBias);
        m_forward.SetInt(m_locDebugShadowClip, m_debugShadowClip);
        for (int i = 0; i < m_cascades; ++i)
        {
            m_forward.SetMat4(m_locCascadeMat0 + i, m_cascadeMat[i].x);
            m_forward.SetFloat(m_locSplits0 + i, m_splits[i + 1]);
        }
    }

    const Material* prevMat = nullptr;
    for (const RenderItem& item : m_items)
    {
        const Material* mat = item.material;
        if (mat != prevMat)
        {
            Vec3 color = mat ? mat->base_color : Vec3(0.8f, 0.5f, 0.35f);
            gl::Texture* diffuse = (mat && mat->diffuse) ? mat->diffuse : &m_white;
            diffuse->Bind(0);
            gl::Texture* detail = (mat && mat->detail) ? mat->detail : &m_gray;
            detail->Bind(6);
            m_forward.SetFloat("u_detailScale", mat ? mat->detail_scale : 1.f);
            gl::Renderer::SetCull((mat && mat->double_sided) ? gl::CullMode::NONE : gl::CullMode::BACK);
            m_forward.SetFloat(m_locUnlit, (mat && mat->unlit) ? 1.f : 0.f);
            m_forward.SetVec2(m_locSpecular, mat ? mat->specular : 0.f, mat ? mat->shininess : 32.f);
            m_forward.SetVec3(m_locColor, color.x, color.y, color.z);
            prevMat = mat;
        }
        m_forward.SetMat4(m_locModel, item.world.x);
        item.vao->Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, item.index_count,
                                  item.first_index);
    }

    // ── BSP lightmapped pass ──
    if (!m_bspInstances.empty())
    {
        m_bsp.Bind();
        prevMat = nullptr;
        gl::Renderer::SetDepthTest(true);
        gl::Renderer::SetDepthWrite(true);
        gl::Renderer::SetBlend(false);

        // Opaque pass first (blend-flagged surfaces — glass/flames/fences,
        // see BspInstance materials' `blend`/`additive`, set from
        // surfaceparm trans / a non-opaque blendFunc in BSPLoader.cpp's
        // shaderInfoMap — are skipped here and drawn in a second pass
        // below). Same two-pass idea as any forward renderer: blended
        // surfaces need the opaque depth buffer already written so they
        // sort against real geometry, and must not write depth themselves
        // or they'd occlude whatever's drawn behind them next.
        auto drawOpaque = [&](Mesh* mesh, const std::vector<Material*>& mats, const Mat4& world)
        {
            if (!mesh) return;
            for (u32 s = 0; s < (u32)mesh->surfaces().size(); ++s)
            {
                const Surface& surf = mesh->surfaces()[s];
                const Material* mat = (surf.material_slot >= 0 && surf.material_slot < (int)mats.size())
                                          ? mats[surf.material_slot]
                                          : (!mats.empty() ? mats[0] : nullptr);
                if (mat && mat->blend) continue;
                if (mat != prevMat)
                {
                    Vec3 color = mat ? mat->base_color : Vec3(1.f, 1.f, 1.f);
                    gl::Texture* diffuse = (mat && mat->diffuse) ? mat->diffuse : &m_white;
                    diffuse->Bind(0);
                    gl::Texture* detail = (mat && mat->detail) ? mat->detail : &m_bspNeutralLM;
                    detail->Bind(6);
                    gl::Renderer::SetCull((mat && mat->double_sided) ? gl::CullMode::NONE : gl::CullMode::BACK);
                    m_bsp.SetVec3(m_locBspColor, color.x, color.y, color.z);
                    prevMat = mat;
                }
                m_bsp.SetMat4(m_locBspModel, world.x);
                mesh->vao().Bind();
                gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, surf.index_count,
                                          surf.first_index);
            }
        };
        for (BspInstance* bsp : m_bspInstances)
            drawOpaque(bsp->get_mesh(), bsp->get_materials(), bsp->get_world_matrix());
        for (BspEntityInstance* mover : m_bspEntityInstances)
            drawOpaque(mover->get_mesh(), mover->get_materials(), mover->get_world_matrix());

        // Blend pass: no depth write, no backface cull (seen from both
        // sides). Not sorted back-to-front — fine for the sparse, mostly
        // convex torches/glass/fences this covers; would need real sorting
        // if this ever has to handle large overlapping blended surfaces.
        gl::Renderer::SetBlend(true);
        gl::Renderer::SetDepthWrite(false);
        gl::Renderer::SetCull(gl::CullMode::NONE);
        prevMat = nullptr;
        bool wasAdditive = false;
        bool blendFactorsSet = false; // force an explicit SetBlendFactors on
                                       // the FIRST blended material this pass
                                       // — comparing mat->additive against
                                       // wasAdditive's initial `false` alone
                                       // meant a non-additive (alpha) first
                                       // material never triggered the call at
                                       // all, leaving blend factors as
                                       // whatever GL state some unrelated
                                       // earlier draw (or a previous frame)
                                       // last left them at.
        auto drawBlend = [&](Mesh* mesh, const std::vector<Material*>& mats, const Mat4& world)
        {
            if (!mesh) return;
            for (u32 s = 0; s < (u32)mesh->surfaces().size(); ++s)
            {
                const Surface& surf = mesh->surfaces()[s];
                const Material* mat = (surf.material_slot >= 0 && surf.material_slot < (int)mats.size())
                                          ? mats[surf.material_slot]
                                          : (!mats.empty() ? mats[0] : nullptr);
                if (!mat || !mat->blend) continue;
                if (mat != prevMat)
                {
                    if (!blendFactorsSet || mat->additive != wasAdditive)
                    {
                        if (mat->additive)
                            gl::Renderer::SetBlendFactors(gl::BlendFactor::ONE, gl::BlendFactor::ONE);
                        else
                            gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA,
                                                          gl::BlendFactor::ONE_MINUS_SRC_ALPHA);
                        wasAdditive = mat->additive;
                        blendFactorsSet = true;
                    }
                    Vec3 color = mat->base_color;
                    gl::Texture* diffuse = mat->diffuse ? mat->diffuse : &m_white;
                    diffuse->Bind(0);
                    gl::Texture* detail = mat->detail ? mat->detail : &m_bspNeutralLM;
                    detail->Bind(6);
                    m_bsp.SetVec3(m_locBspColor, color.x, color.y, color.z);
                    prevMat = mat;
                }
                m_bsp.SetMat4(m_locBspModel, world.x);
                mesh->vao().Bind();
                gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, surf.index_count,
                                          surf.first_index);
            }
        };
        for (BspInstance* bsp : m_bspInstances)
            drawBlend(bsp->get_mesh(), bsp->get_materials(), bsp->get_world_matrix());
        for (BspEntityInstance* mover : m_bspEntityInstances)
            drawBlend(mover->get_mesh(), mover->get_materials(), mover->get_world_matrix());
        gl::Renderer::SetBlend(false);
        gl::Renderer::SetDepthWrite(true);
        gl::Renderer::SetCull(gl::CullMode::BACK);
    }

    // splat-mode paged terrain: opaque, in-view like any item (reflections
    // and refraction included, clipped by the view's plane)
    if (!m_pagedTerrains.empty()) draw_paged_terrain(v, frustum);

    // skinned characters: opaque, forward-lit, per-instance bone palette
    if (!m_skinnedMeshes.empty()) draw_skinned(v, frustum);

    if (m_sky_enabled || m_skybox || m_skydome)
    {
        // background: fills every pixel the opaque pass left at depth 1.0.
        // The sky shaders write no clip distance, so the hardware plane
        // must be off while they draw. Three kinds (Ogre-style), one pass:
        // procedural gradient, cubemap skybox or equirect skydome — all
        // sample by the per-pixel view direction, no geometry.
        gl::Renderer::SetClipDistance(0, false);
        gl::Renderer::SetDepthWrite(false);
        gl::Renderer::SetDepthFunction(gl::DepthFunction::LEQUAL);
        gl::Renderer::SetCull(gl::CullMode::NONE);

        gl::Shader* sky = &m_sky;
        if (m_skybox)
        {
            sky = &m_skyboxShader;
            m_skybox->Bind(0);
        }
        else if (m_skydome)
        {
            sky = &m_skydomeShader;
            m_skydome->Bind(0);
        }
        sky->Bind();
        Mat4 invViewProj = Mat4::Inverse(vp);
        sky->SetMat4("u_invViewProj", invViewProj.x);
        sky->SetVec3("u_cameraPos", v.cam_pos.x, v.cam_pos.y, v.cam_pos.z);
        if (sky == &m_sky) sky->SetVec3("u_sunDir", -m_lightDir.x, -m_lightDir.y, -m_lightDir.z);
        gl::Renderer::DrawFullscreenTriangle();
        gl::Renderer::SetDepthFunction(gl::DepthFunction::LESS);
        gl::Renderer::SetDepthWrite(true);
    }
}

// The ocean shader (assets/shaders/water.*) drives its own uniforms: the
// grid is displaced by the Gerstner waves, the fragment pass reads the same
// reflection/refraction targets plus bump and foam textures.
void SceneRenderer::draw_ocean_surface(OceanNode* o, const Mat4& view, const Mat4& proj,
                                       const Vec3& cameraPos)
{
    m_oceanShader.Bind();
    m_oceanShader.SetMat4("model", o->get_global_transform().x);
    m_oceanShader.SetMat4("view", view.x);
    m_oceanShader.SetMat4("projection", proj.x);
    m_oceanShader.SetVec3("cameraPos", cameraPos.x, cameraPos.y, cameraPos.z);
    m_oceanShader.SetVec3("u_cameraPosition", cameraPos.x, cameraPos.y, cameraPos.z);
    m_oceanShader.SetFloat("u_time", o->time());
    m_oceanShader.SetVec4("u_wave1", o->wave1.x, o->wave1.y, o->wave1.z, o->wave1.w);
    m_oceanShader.SetVec4("u_wave2", o->wave2.x, o->wave2.y, o->wave2.z, o->wave2.w);
    m_oceanShader.SetVec4("u_wave3", o->wave3.x, o->wave3.y, o->wave3.z, o->wave3.w);
    m_oceanShader.SetVec4("u_wave4", o->wave4.x, o->wave4.y, o->wave4.z, o->wave4.w);
    m_oceanShader.SetFloat("u_waveHeight", o->wave_height);
    m_oceanShader.SetFloat("u_waveLength", o->bump_uv_scale);
    m_oceanShader.SetFloat("u_windForce", o->wind_force);
    m_oceanShader.SetVec2("u_windDirection", o->wind_dir.x, o->wind_dir.y);
    m_oceanShader.SetVec4("u_waterColor", o->ocean_color.x, o->ocean_color.y, o->ocean_color.z,
                          o->ocean_color.w);
    m_oceanShader.SetFloat("u_colorBlendFactor", o->color_blend);
    m_oceanShader.SetFloat("mult", o->depth_scale);
    m_oceanShader.SetFloat("u_foamRange", o->foam_range);
    m_oceanShader.SetFloat("u_foamScale", o->foam_scale);
    m_oceanShader.SetFloat("u_foamSpeed", o->foam_speed);
    m_oceanShader.SetFloat("u_foamIntensity", o->foam_intensity);
    m_oceanShader.SetFloat("u_shoreFade", o->shore_fade);

    o->reflection_tex().Bind(0);
    o->refraction_tex().Bind(1);
    o->refraction_depth_tex().Bind(2);
    o->ensure_textures();
    if (o->bump)
        o->bump->Bind(3);
    else
        o->builtin_bump().Bind(3);
    if (o->foam)
        o->foam->Bind(4);
    else
        o->builtin_foam().Bind(4);

    o->quad().vao().Bind();
    gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, o->quad().index_count());
}

void SceneRenderer::draw_water_surfaces(const Mat4& view, const Mat4& proj, const Vec3& cameraPos,
                                        float camNear, float camFar)
{
    Mat4 viewProj = proj * view;
    m_water.Bind();
    m_water.SetMat4(m_locWViewProj, viewProj.x);
    m_water.SetVec3(m_locWCameraPos, cameraPos.x, cameraPos.y, cameraPos.z);
    m_water.SetVec3(m_locWLightDir, m_lightDir.x, m_lightDir.y, m_lightDir.z);
    m_water.SetVec2(m_locWCamPlanes, camNear, camFar);

    // every surface alpha-fades at the waterline (soft shoreline) — one
    // blend setup for the whole pass
    gl::Renderer::SetBlend(true);
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);

    for (WaterNode* w : m_waters)
    {
        if (!w->quad().is_uploaded()) continue;

        OceanNode* ocean = w->as<OceanNode>();
        if (ocean)
        {
            if (m_ocean_ready)
            {
                draw_ocean_surface(ocean, view, proj, cameraPos);
                m_water.Bind(); // back to the plain water shader for the rest
            }
            continue;
        }

        w->reflection_tex().Bind(0);
        w->refraction_tex().Bind(1);
        w->refraction_depth_tex().Bind(2);
        m_water.SetFloat(m_locWTime, w->time() * (w->wave_speed * 40.f));
        m_water.SetVec3(m_locWColor, w->water_color.x, w->water_color.y, w->water_color.z);
        m_water.SetFloat(m_locWDistortion, w->distortion);
        m_water.SetFloat(m_locWTiling, w->wave_tiling);
        m_water.SetFloat(m_locWColorMix, w->color_mix);
        m_water.SetMat4(m_locWModel, w->get_global_transform().x);
        w->quad().vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, w->quad().index_count());
    }
    gl::Renderer::SetBlend(false);
}

void SceneRenderer::draw_mirror_surfaces(const Mat4& view, const Mat4& proj, const Vec3& cameraPos)
{
    Mat4 viewProj = proj * view;
    m_mirror.Bind();
    m_mirror.SetMat4(m_locMViewProj, viewProj.x);
    m_mirror.SetVec3(m_locMCameraPos, cameraPos.x, cameraPos.y, cameraPos.z);

    for (MirrorNode* m : m_mirrors)
    {
        if (!m->quad().is_uploaded()) continue;

        m->reflection_tex().Bind(0);
        m_mirror.SetVec3(m_locMTint, m->tint.x, m->tint.y, m->tint.z);
        m_mirror.SetFloat(m_locMReflectivity, m->reflectivity);
        Vec3 n = m->plane_normal();
        m_mirror.SetVec3(m_locMNormal, n.x, n.y, n.z);
        m_mirror.SetMat4(m_locMModel, m->get_global_transform().x);
        m->quad().vao().Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, m->quad().index_count());
    }
}

void SceneRenderer::build_spatial_index(Scene& scene)
{
    m_octree.build(&scene.root());
    m_octree_built = true;
    if (getenv("COREGL_OCTREE_DIAG"))
    {
        std::vector<const SceneOctreeNode*> nodes;
        collect_octree_bounds(m_octree.root(), nodes);
        int maxDepth = 0, totalEntries = 0;
        for (const SceneOctreeNode* n : nodes)
        {
            if (n->depth > maxDepth) maxDepth = n->depth;
            totalEntries += (int)n->entries.size();
        }
        printf("octree diag: %d occupied nodes, max depth %d, %d entries indexed\n",
               (int)nodes.size(), maxDepth, totalEntries);
    }
}

void SceneRenderer::render_fit(Scene& scene, int windowW, int windowH, int virtualW, int virtualH,
                               ViewportFitMode mode)
{
    if (!m_ready) return;
    ViewportRect r = compute_viewport_fit(windowW, windowH, virtualW, virtualH, mode);
    m_outputX = r.x;
    m_outputY = r.y;
    m_outputW = r.w;
    m_outputH = r.h;

    render(scene, virtualW, virtualH);

    // blacken the leftover bars (letterbox/crop) AFTER rendering, not
    // before: draw_view()'s own internal clear (when the main view targets
    // the real screen, i.e. post-processing off) has no scissor and clears
    // the *whole* bound framebuffer regardless of the viewport rect — doing
    // this first meant that clear silently wiped the bars back to
    // m_clearColor every frame. Nothing drawn by render() above can ever
    // land outside the viewport rect (that part of the original reasoning
    // was right), so doing this last is always safe.
    bool coversWindow = r.x <= 0 && r.y <= 0 && r.w >= windowW && r.h >= windowH;
    if (!coversWindow)
    {
        gl::Renderer::BindScreen();
        gl::Renderer::SetScissor(true);
        gl::Renderer::ClearColor(0.f, 0.f, 0.f, 1.f);
        if (r.x > 0) // pillarbox: bars left + right
        {
            gl::Renderer::SetScissorRect(0, 0, r.x, windowH);
            gl::Renderer::Clear(true, false);
            gl::Renderer::SetScissorRect(r.x + r.w, 0, windowW - (r.x + r.w), windowH);
            gl::Renderer::Clear(true, false);
        }
        if (r.y > 0) // letterbox: bars top + bottom
        {
            gl::Renderer::SetScissorRect(0, 0, windowW, r.y);
            gl::Renderer::Clear(true, false);
            gl::Renderer::SetScissorRect(0, r.y + r.h, windowW, windowH - (r.y + r.h));
            gl::Renderer::Clear(true, false);
        }
        gl::Renderer::SetScissor(false);
    }

    // restore so a later plain render() call (if any) isn't left offset
    m_outputX = 0;
    m_outputY = 0;
    m_outputW = 0;
    m_outputH = 0;
}

void SceneRenderer::render(Scene& scene, int viewport_w, int viewport_h)
{
    if (!m_ready) return;
    Camera3D* camera = scene.get_active_camera();

    if (!camera) return; // nothing to see with

    camera->set_viewport_size(viewport_w, viewport_h);
    Mat4 proj = camera->get_projection_matrix();
    Mat4 view = camera->get_view_matrix();
    Vec3 cameraPos = camera->get_global_position();

    // reset GL stats each frame so the overlay shows per-frame counts
    gl::Renderer::ResetStats();

    // collected before the shadow pass: splat pages cast shadows too
    m_pagedTerrains.clear();
    collect_paged_terrain(&scene.root(), m_pagedTerrains);
    m_skinnedMeshes.clear();
    collect_skinned(&scene.root(), m_skinnedMeshes);

    // ── shadow views: one depth-only view per cascade ──
    if (m_cascades > 0) draw_shadow_views(scene, camera);

    // ── local lights: point cubemaps + spot maps ──
    m_lights.clear();
    collect_lights(&scene.root(), m_lights);
    if (!m_lights.empty()) draw_light_shadows(scene);

    m_waters.clear();
    collect_water(&scene.root(), m_waters);

    // ── extra views: one reflection + one refraction per water surface ──
    for (WaterNode* w : m_waters)
    {
        if (!w->ensure_gpu(viewport_w / 2, viewport_h / 2)) continue;
        const float h = w->surface_height();

        // Reflected camera: mirror the world across the plane y = h
        // (y' = 2h - y), then flip view-space Y so the camera keeps its up
        // vector — equivalent to placing the camera below the surface with
        // its pitch inverted. The two flips cancel: the result is a proper
        // rotation, so triangle winding is unchanged.
        Mat4 mirror = Mat4::Identity();
        mirror.x[5] = -1.f;
        mirror.x[13] = 2.f * h;
        Mat4 flipY = Mat4::Identity();
        flipY.x[5] = -1.f;

        RenderView refl;
        refl.view = flipY * (view * mirror);
        refl.proj = proj;
        refl.target = &w->reflection_fbo();
        refl.w = w->target_width();
        refl.h = w->target_height();
        // keep what is above the surface, plus a small band below it —
        // distorted UVs at the shoreline must never sample clipped (clear
        // color) texels, or the water edge picks up the sky color
        refl.clip_plane = Vec4(0.f, 1.f, 0.f, -h + 0.6f);
        refl.use_clip = true;
        refl.cam_pos = Vec3(cameraPos.x, 2.f * h - cameraPos.y, cameraPos.z);
        draw_view(scene, refl);

        RenderView refr;
        refr.view = view;
        refr.proj = proj;
        refr.target = &w->refraction_fbo();
        refr.w = w->target_width();
        refr.h = w->target_height();
        // keep what is below the surface, plus a band above it — the shore
        // continues into the refraction texture instead of clipping to sky
        refr.clip_plane = Vec4(0.f, -1.f, 0.f, h + 0.5f);
        refr.use_clip = true;
        refr.cam_pos = cameraPos;
        draw_view(scene, refr);
    }

    m_mirrors.clear();
    collect_mirrors(&scene.root(), m_mirrors);

    // ── extra views: one reflection per mirror surface, no refraction ──
    // General plane orientation (unlike WaterNode's horizontal-only trick):
    // reflect the camera's own position/forward/up across the mirror's
    // plane and build a normal LookAt view from that — the standard planar
    // reflection technique. Reflecting a right-handed basis flips
    // handedness, so triangle winding comes out backwards; refl.mirrored
    // tells draw_view to flip front-face culling to compensate (see
    // "a mirrored view reverses triangle winding" below).
    for (MirrorNode* m : m_mirrors)
    {
        if (!m->ensure_gpu(viewport_w / 2, viewport_h / 2)) continue;

        Vec3 planePoint = m->plane_point();
        Vec3 planeNormal = m->plane_normal();

        auto reflectPoint = [&](const Vec3& p) {
            return p - planeNormal * (2.f * Vec3::Dot(p - planePoint, planeNormal));
        };
        auto reflectDir = [&](const Vec3& v) {
            return v - planeNormal * (2.f * Vec3::Dot(v, planeNormal));
        };

        Vec3 reflCamPos = reflectPoint(cameraPos);
        Vec3 reflCamForward = reflectDir(camera->global_forward());
        Vec3 reflCamUp = reflectDir(camera->global_up());

        RenderView refl;
        refl.view = Mat4::LookAt(reflCamPos, reflCamPos + reflCamForward, reflCamUp);
        refl.proj = proj;
        refl.target = &m->reflection_fbo();
        refl.w = m->target_width();
        refl.h = m->target_height();
        // keep the side the real camera is on (a small band past the plane
        // too, so distorted/edge sampling never picks up clipped texels)
        refl.clip_plane = Vec4(planeNormal.x, planeNormal.y, planeNormal.z,
                               -Vec3::Dot(planePoint, planeNormal) + 0.05f);
        refl.use_clip = true;
        refl.mirrored = true;
        refl.cam_pos = reflCamPos;
        draw_view(scene, refl);
    }

    // ── main view (into the HDR target when post-processing is on) ──
    bool post = m_post_enabled && ensure_post_targets(viewport_w, viewport_h);
    RenderView main_view;
    main_view.view = view;
    main_view.proj = proj;
    main_view.target = post ? &m_hdrFbo : nullptr;
    main_view.w = viewport_w;
    main_view.h = viewport_h;
    main_view.cam_pos = cameraPos;
    if (m_wireframe) gl::Renderer::SetWireframe(true);
    draw_view(scene, main_view);
    if (m_wireframe) gl::Renderer::SetWireframe(false);

    m_grassSystems.clear();
    collect_grass(&scene.root(), m_grassSystems);
    if (!m_grassSystems.empty()) draw_grass(proj * view);

    m_treeSystems.clear();
    collect_trees(&scene.root(), m_treeSystems);
    if (!m_treeSystems.empty()) draw_trees(proj * view);

    // water surfaces draw last, depth-tested against the scene (they land
    // in whatever target the main view used)
    if (!m_waters.empty())
        draw_water_surfaces(view, proj, cameraPos, camera->get_near(), camera->get_far());
    if (!m_mirrors.empty()) draw_mirror_surfaces(view, proj, cameraPos);

    // particles: billboard toward the active camera, then draw over
    // everything opaque/water already in this target
    m_particleSystems.clear();
    collect_particles(&scene.root(), m_particleSystems);
    m_decalSystems.clear();
    collect_decals(&scene.root(), m_decalSystems);
    m_ribbonTrails.clear();
    collect_ribbontrails(&scene.root(), m_ribbonTrails);
    m_billboards.clear();
    collect_billboards(&scene.root(), m_billboards);
    if (!m_particleSystems.empty() || !m_decalSystems.empty() || !m_ribbonTrails.empty() ||
        !m_billboards.empty())
    {
        Vec3 camRight = Mat4(camera->get_global_rotation()) * Vec3(1.f, 0.f, 0.f);
        Vec3 camUp = Mat4(camera->get_global_rotation()) * Vec3(0.f, 1.f, 0.f);
        Vec3 camForward = Mat4(camera->get_global_rotation()) * Vec3(0.f, 0.f, -1.f);
        for (ParticleSystemNode* ps : m_particleSystems)
            ps->build_billboards(camRight, camUp);
        for (RibbonTrailNode* rt : m_ribbonTrails)
            rt->rebuild(cameraPos, camUp);
        for (BillboardNode* b : m_billboards)
            b->rebuild(camRight, camUp, camForward);
        draw_particles(proj * view);
        draw_ribbontrails(proj * view);
        draw_billboards(proj * view);
    }

    // lens flares: screen-space, additive, on top of everything
    m_lensflares.clear();
    collect_lensflares(&scene.root(), m_lensflares);
    if (!m_lensflares.empty()) draw_lensflares(*camera);

    if (post)
    {
        gl::Renderer::SetDepthTest(false);
        gl::Renderer::SetCull(gl::CullMode::NONE);

        // two ping-pong buffers (m_hdrFbo/m_hdrColor, m_pingFbo/m_pingColor)
        // carry the color through however many of these stages are active —
        // each stage reads colorSrc and writes into whichever buffer ISN'T
        // colorSrc's owner, then hands that buffer to the next stage.
        auto otherTarget = [&](const gl::Texture* src) -> gl::FrameBuffer* {
            return src == &m_hdrColor ? &m_pingFbo : &m_hdrFbo;
        };
        auto otherTexture = [&](const gl::Texture* src) -> const gl::Texture* {
            return src == &m_hdrColor ? &m_pingColor : &m_hdrColor;
        };

        const gl::Texture* colorSrc = &m_hdrColor;

        if (m_ssao_enabled)
        {
            // raw hemisphere AO from the depth buffer into m_ssaoColor
            m_ssaoFbo.Bind();
            gl::Renderer::Viewport(0, 0, viewport_w, viewport_h);
            m_ssao.Bind();
            m_hdrDepth.Bind(0);
            m_ssaoNoise.Bind(1);
            m_hdrNormal.Bind(2);
            m_ssao.SetMat4("u_proj", proj.x);
            Mat4 invProj = Mat4::Inverse(proj);
            m_ssao.SetMat4("u_invProj", invProj.x);
            m_ssao.SetVec2("u_noiseScale", (float)viewport_w / 8.f, (float)viewport_h / 8.f);
            m_ssao.SetFloat("u_radius", m_ssaoRadius);
            m_ssao.SetFloat("u_bias", 0.025f);
            m_ssao.SetFloat("u_strength", m_ssaoStrength);
            gl::Renderer::DrawFullscreenTriangle();

            // blur the noise out and multiply straight into the color chain
            gl::FrameBuffer* dst = otherTarget(colorSrc);
            dst->Bind();
            gl::Renderer::Viewport(0, 0, viewport_w, viewport_h);
            m_ssaoBlur.Bind();
            m_ssaoColor.Bind(0);
            const_cast<gl::Texture*>(colorSrc)->Bind(1);
            m_ssaoBlur.SetVec2("u_texelSize", 1.f / (float)viewport_w, 1.f / (float)viewport_h);
            gl::Renderer::DrawFullscreenTriangle();
            colorSrc = otherTexture(colorSrc);
        }

        if (m_godrays_enabled && m_cascades > 0)
        {
            // volumetric sun: march the LAST cascade through the scene depth
            gl::FrameBuffer* dst = otherTarget(colorSrc);
            dst->Bind();
            gl::Renderer::Viewport(0, 0, viewport_w, viewport_h);
            m_godray.Bind();
            const_cast<gl::Texture*>(colorSrc)->Bind(0);
            m_hdrDepth.Bind(1);
            m_shadowTex.Bind(2);
            const int last = m_cascades - 1;
            m_godray.SetMat4("u_lightMat", m_cascadeMat[last].x);
            m_godray.SetFloat("u_lastLayer", (float)last);
            Mat4 invViewProj = Mat4::Inverse(proj * view);
            m_godray.SetMat4("u_invViewProj", invViewProj.x);
            m_godray.SetVec3("u_cameraPos", cameraPos.x, cameraPos.y, cameraPos.z);
            m_godray.SetVec3("u_lightDir", m_lightDir.x, m_lightDir.y, m_lightDir.z);
            m_godray.SetVec3("u_lightColor", 1.0f, 0.92f, 0.78f);
            m_godray.SetVec4("u_params", 24.f, 1.4f, 1.10f, 900.f);
            m_godray.SetFloat("u_asymmetry", 0.7f);
            gl::Renderer::DrawFullscreenTriangle();
            colorSrc = otherTexture(colorSrc);
        }

        if (m_bloom_enabled)
        {
            // 1) bright-pass extract, full-res source into level 0
            m_bloomFbo[0].Bind();
            gl::Renderer::Viewport(0, 0, m_bloomMipW[0], m_bloomMipH[0]);
            m_bloomExtract.Bind();
            const_cast<gl::Texture*>(colorSrc)->Bind(0);
            m_bloomExtract.SetFloat("u_threshold", m_bloomThreshold);
            m_bloomExtract.SetFloat("u_exposure", m_exposure);
            gl::Renderer::DrawFullscreenTriangle();

            // 2) downsample the rest of the chain, level i-1 -> level i
            m_bloomDownsample.Bind();
            for (int i = 1; i < kBloomMips; ++i)
            {
                m_bloomFbo[i].Bind();
                gl::Renderer::Viewport(0, 0, m_bloomMipW[i], m_bloomMipH[i]);
                m_bloomColor[i - 1].Bind(0);
                gl::Renderer::DrawFullscreenTriangle();
            }

            // 3) blur each level in place (horizontal into the scratch
            // buffer, vertical back into m_bloomColor[i]) — small-radius
            // taps, but each level is already a fraction of the size of
            // the one before, so the same taps cover proportionally more
            // of the image the smaller the level gets
            m_bloomBlur.Bind();
            for (int i = 0; i < kBloomMips; ++i)
            {
                int mw = m_bloomMipW[i], mh = m_bloomMipH[i];
                gl::Renderer::Viewport(0, 0, mw, mh);

                m_bloomBlurFbo[i].Bind();
                m_bloomColor[i].Bind(0);
                m_bloomBlur.SetVec2("u_texelSize", 1.f / (float)mw, 0.f);
                gl::Renderer::DrawFullscreenTriangle();

                m_bloomFbo[i].Bind();
                m_bloomBlurColor[i].Bind(0);
                m_bloomBlur.SetVec2("u_texelSize", 0.f, 1.f / (float)mh);
                gl::Renderer::DrawFullscreenTriangle();
            }

            // 4) upsample-add smallest to largest — each add samples the
            // smaller (already-combined) level at the bigger level's UVs,
            // which is a free bilinear upsample, into the scratch buffer
            // that level's blur pass just finished using
            m_bloomUpsample.Bind();
            const gl::Texture* accum = &m_bloomColor[kBloomMips - 1];
            for (int i = kBloomMips - 2; i >= 0; --i)
            {
                m_bloomBlurFbo[i].Bind();
                gl::Renderer::Viewport(0, 0, m_bloomMipW[i], m_bloomMipH[i]);
                m_bloomColor[i].Bind(0);
                const_cast<gl::Texture*>(accum)->Bind(1);
                gl::Renderer::DrawFullscreenTriangle();
                accum = &m_bloomBlurColor[i];
            }

            // 5) additive composite back onto the full-res color chain
            gl::FrameBuffer* dst = otherTarget(colorSrc);
            dst->Bind();
            gl::Renderer::Viewport(0, 0, viewport_w, viewport_h);
            m_bloomComposite.Bind();
            const_cast<gl::Texture*>(colorSrc)->Bind(0);
            const_cast<gl::Texture*>(accum)->Bind(1);
            m_bloomComposite.SetFloat("u_intensity", m_bloomIntensity);
            gl::Renderer::DrawFullscreenTriangle();
            colorSrc = otherTexture(colorSrc);
        }

        // filmic tonemap to the screen — offset+scaled by render_fit()'s
        // rect if in effect, same as draw_view()'s own screen-target branch
        gl::Renderer::BindScreen();
        gl::Renderer::Viewport(m_outputX, m_outputY, m_outputW > 0 ? m_outputW : viewport_w,
                               m_outputH > 0 ? m_outputH : viewport_h);
        m_tonemap.Bind();
        const_cast<gl::Texture*>(colorSrc)->Bind(0);
        m_tonemap.SetFloat("u_exposure", m_exposure);
        gl::Renderer::DrawFullscreenTriangle();
        gl::Renderer::SetDepthTest(true);
    }

    if (m_debug_views) draw_debug_views(viewport_w, viewport_h);

    if (m_show_light_gizmos) draw_light_gizmos(proj * view);
    if (m_show_octree_debug) draw_octree_debug(proj * view);
    if (m_show_mesh_bounds) draw_mesh_bounds_debug(scene.root(), proj * view);
    draw_debug_surface_index(proj * view);

    // snapshot BEFORE the stats panel draws, so the panel reports the
    // scene's cost, not its own
    m_frameStats = gl::Renderer::GetStats();

    // frame clock: time between render() calls, smoothed for a stable readout
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const gl::u64 ns =
            (gl::u64)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        if (m_lastFrameNs)
        {
            const float ms = (float)(ns - m_lastFrameNs) / 1e6f;
            m_smoothMs = m_smoothMs > 0.f ? m_smoothMs * 0.95f + ms * 0.05f : ms;
        }
        m_lastFrameNs = ns;
    }

    if (m_show_stats) draw_stats(viewport_w, viewport_h);
}

// semi-transparent panel with the frame's RenderStats, drawn with the 2D
// Batch and its embedded 8x8 font — engine-side so every game gets it
void SceneRenderer::draw_stats(int viewport_w, int viewport_h)
{
    if (!m_statsBatchReady)
    {
        if (!m_statsBatch.Init()) return;
        m_statsBatchReady = true;
    }

    const gl::RenderStats& s = m_frameStats;
    char lines[9][64];
    int n = 0;
    if (m_smoothMs > 0.f)
        snprintf(lines[n++], 64, "fps         %.0f (%.2f ms)", 1000.f / m_smoothMs, m_smoothMs);
    snprintf(lines[n++], 64, "items       %d", m_last_items);
    snprintf(lines[n++], 64, "draw calls  %llu", (unsigned long long)s.drawCalls);
    snprintf(lines[n++], 64, "triangles   %llu", (unsigned long long)s.triangles);
    snprintf(lines[n++], 64, "shader swap %llu", (unsigned long long)s.shaderSwitches);
    snprintf(lines[n++], 64, "tex binds   %llu", (unsigned long long)s.textureBinds);
    snprintf(lines[n++], 64, "vao/fbo     %llu/%llu", (unsigned long long)s.vaoSwitches,
             (unsigned long long)s.fboSwitches);
    snprintf(lines[n++], 64, "state chg   %llu", (unsigned long long)s.stateChanges);

    const float ts = 14.f; // text size (pixels per glyph cell)
    const float pad = 10.f, lineH = ts + 4.f;
    float wMax = 0.f;
    for (int i = 0; i < n; ++i)
        wMax = std::max(wMax, m_statsBatch.TextWidth(ts, lines[i]));

    Mat4 ortho = Mat4::Ortho(0.f, (float)viewport_w, (float)viewport_h, 0.f, -1.f, 1.f);
    m_statsBatch.SetProjection(ortho.x);

    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(true);
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);

    m_statsBatch.SetColor(10, 14, 20, 190);
    m_statsBatch.Rect(8.f, 8.f, wMax + pad * 2.f, (float)n * lineH + pad * 2.f);
    m_statsBatch.SetColor(140, 235, 140, 255);
    for (int i = 0; i < n; ++i)
        m_statsBatch.Text(8.f + pad, 8.f + pad + (float)i * lineH, ts, lines[i]);
    m_statsBatch.Render();

    gl::Renderer::SetBlend(false);
    gl::Renderer::SetDepthTest(true);
}

// wireframe sphere at every light's world position (radius = range for
// point/spot; a fixed small marker for anything else) — see where a light
// actually sits instead of guessing from the lit result on surfaces
void SceneRenderer::draw_light_gizmos(const Mat4& viewProj)
{
    if (m_lights.empty()) return;
    if (!m_gizmoBatchReady)
    {
        if (!m_gizmoBatch.Init()) return;
        m_gizmoBatchReady = true;
    }

    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(false);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(false);

    m_gizmoBatch.SetProjection(viewProj.x);
    m_gizmoBatch.LoadIdentity();
    m_gizmoBatch.SetMode(gl::RenderPrimitive::LINES);

    for (LightNode* light : m_lights)
    {
        Vec3 p = light->get_global_position();
        float range = 0.4f;
        if (PointLight* pt = light->as<PointLight>())
            range = pt->range;
        else if (SpotLight* sp = light->as<SpotLight>())
            range = sp->range;

        gl::u8 r = (gl::u8)(light->color.x * 255.f), g = (gl::u8)(light->color.y * 255.f),
               b = (gl::u8)(light->color.z * 255.f);
        m_gizmoBatch.SetColor(r, g, b, 255);
        m_gizmoBatch.SphereWire(p.x, p.y, p.z, range * 0.05f); // small marker at the light itself
        m_gizmoBatch.SetColor(r, g, b, 90);
        m_gizmoBatch.SphereWire(p.x, p.y, p.z, range); // full range, dim
    }

    m_gizmoBatch.Render();
    gl::Renderer::SetDepthWrite(true);
}

void SceneRenderer::draw_wire_sphere(Camera3D& camera, const Vec3& center, float radius, gl::u8 r, gl::u8 g,
                                     gl::u8 b, gl::u8 a)
{
    if (!m_gizmoBatchReady)
    {
        if (!m_gizmoBatch.Init()) return;
        m_gizmoBatchReady = true;
    }

    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(false);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(a < 255);
    if (a < 255) gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);

    Mat4 viewProj = camera.get_view_projection();
    m_gizmoBatch.SetProjection(viewProj.x);
    m_gizmoBatch.LoadIdentity();
    m_gizmoBatch.SetMode(gl::RenderPrimitive::LINES);
    m_gizmoBatch.SetColor(r, g, b, a);
    m_gizmoBatch.SphereWire(center.x, center.y, center.z, radius);
    m_gizmoBatch.Render();
}

void SceneRenderer::draw_wire_box(Camera3D& camera, const Vec3& center, const Vec3& halfExtent,
                                  gl::u8 r, gl::u8 g, gl::u8 b, gl::u8 a)
{
    if (!m_gizmoBatchReady)
    {
        if (!m_gizmoBatch.Init()) return;
        m_gizmoBatchReady = true;
    }

    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(false);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(a < 255);
    if (a < 255) gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);

    Mat4 viewProj = camera.get_view_projection();
    m_gizmoBatch.SetProjection(viewProj.x);
    m_gizmoBatch.LoadIdentity();
    m_gizmoBatch.SetMode(gl::RenderPrimitive::LINES);
    m_gizmoBatch.SetColor(r, g, b, a);
    m_gizmoBatch.CubeWire(center.x, center.y, center.z, halfExtent.x * 2.f, halfExtent.y * 2.f,
                         halfExtent.z * 2.f);
    m_gizmoBatch.Render();

    gl::Renderer::SetBlend(false);
    gl::Renderer::SetDepthWrite(true);
}

void SceneRenderer::draw_world_text(Camera3D& camera, const Vec3& worldPos, const char* text,
                                    int viewport_w, int viewport_h, float size, gl::u8 r, gl::u8 g,
                                    gl::u8 b, gl::u8 a)
{
    if (!m_statsBatchReady)
    {
        if (!m_statsBatch.Init()) return;
        m_statsBatchReady = true;
    }

    Mat4 viewProj = camera.get_view_projection();
    Vec4 clip = viewProj * Vec4(worldPos);
    if (clip.w <= 0.001f) return; // behind the camera — nothing sane to project

    float ndcX = clip.x / clip.w;
    float ndcY = clip.y / clip.w;
    float sx = (ndcX * 0.5f + 0.5f) * (float)viewport_w;
    float sy = (1.f - (ndcY * 0.5f + 0.5f)) * (float)viewport_h;

    Mat4 ortho = Mat4::Ortho(0.f, (float)viewport_w, (float)viewport_h, 0.f, -1.f, 1.f);
    m_statsBatch.SetProjection(ortho.x);

    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetBlend(true);
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA, gl::BlendFactor::ONE_MINUS_SRC_ALPHA);

    m_statsBatch.SetColor(r, g, b, a);
    m_statsBatch.Text(sx, sy, size, text);
    m_statsBatch.Render();

    gl::Renderer::SetBlend(false);
    gl::Renderer::SetDepthTest(true);
}

void SceneRenderer::collect_octree_bounds(const SceneOctreeNode* node,
                                          std::vector<const SceneOctreeNode*>& out)
{
    if (!node) return;
    if (!node->entries.empty()) out.push_back(node);
    for (int i = 0; i < 8; ++i)
        collect_octree_bounds(node->children[i], out);
}

void SceneRenderer::collect_mesh_instances(Node* node, std::vector<MeshInstance*>& out)
{
    MeshInstance* m = node->as<MeshInstance>();
    if (m) out.push_back(m);
    for (Node* child : node->get_children())
        collect_mesh_instances(child, out);
}

// wireframe box per MeshInstance — its Mesh::bounds() (local space)
// transformed into world space by the instance's own transform, same
// technique culling itself uses. One fixed color (unlike the octree debug's
// depth tint): the point here is "where/how big is this thing", not a
// hierarchy to read.
void SceneRenderer::draw_mesh_bounds_debug(Node& root, const Mat4& viewProj)
{
    if (!m_gizmoBatchReady)
    {
        if (!m_gizmoBatch.Init()) return;
        m_gizmoBatchReady = true;
    }

    std::vector<MeshInstance*> instances;
    collect_mesh_instances(&root, instances);
    if (instances.empty()) return;

    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(false);

    m_gizmoBatch.SetProjection(viewProj.x);
    m_gizmoBatch.LoadIdentity();
    m_gizmoBatch.SetMode(gl::RenderPrimitive::LINES);
    m_gizmoBatch.SetColor(60, 220, 255, 220);

    for (MeshInstance* inst : instances)
    {
        Mesh* mesh = inst->get_mesh();
        if (!mesh) continue;
        BoundingBox wb = BoundingBox::TransformBoundingBox(mesh->bounds(), inst->get_world_matrix());
        Vec3 c = wb.center();
        Vec3 sz = wb.max - wb.min;
        m_gizmoBatch.CubeWire(c.x, c.y, c.z, sz.x, sz.y, sz.z);
    }

    m_gizmoBatch.Render();
}

void SceneRenderer::draw_debug_surface_index(const Mat4& viewProj)
{
    if (!m_debugSurfaceInst || m_debugSurfaceIndex < 0) return;
    Mesh* mesh = m_debugSurfaceInst->get_mesh();
    if (!mesh) return;
    const std::vector<Surface>& surfaces = mesh->surfaces();
    if (m_debugSurfaceIndex >= (int)surfaces.size()) return;
    const Surface& s = surfaces[(size_t)m_debugSurfaceIndex];
    if (s.index_count == 0) return; // removed via Mesh::remove_surface

    if (!m_gizmoBatchReady)
    {
        if (!m_gizmoBatch.Init()) return;
        m_gizmoBatchReady = true;
    }

    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(false);

    m_gizmoBatch.SetProjection(viewProj.x);
    m_gizmoBatch.LoadIdentity();
    m_gizmoBatch.SetMode(gl::RenderPrimitive::LINES);
    m_gizmoBatch.SetColor(255, 40, 220, 255);

    BoundingBox wb = BoundingBox::TransformBoundingBox(s.bounds, m_debugSurfaceInst->get_world_matrix());
    Vec3 c = wb.center();
    Vec3 sz = wb.max - wb.min;
    m_gizmoBatch.CubeWire(c.x, c.y, c.z, sz.x, sz.y, sz.z);

    m_gizmoBatch.Render();
}

// wireframe box per SceneOctree node holding at least one instance — depth
// tints the color (root = red, deeper = greener) so the split is readable
void SceneRenderer::draw_octree_debug(const Mat4& viewProj)
{
    if (m_octree.empty()) return;
    if (!m_gizmoBatchReady)
    {
        if (!m_gizmoBatch.Init()) return;
        m_gizmoBatchReady = true;
    }

    std::vector<const SceneOctreeNode*> nodes;
    collect_octree_bounds(m_octree.root(), nodes);
    if (nodes.empty()) return;

    // depth test OFF: these boxes enclose clusters of solid geometry (that's
    // the whole point of looking at them) — with depth test on, a dense
    // scene (octree_stress) buries every box inside the opaque objects it
    // surrounds and you see nothing
 //   gl::Renderer::SetDepthTest(false);
 //   gl::Renderer::SetDepthWrite(false);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetBlend(false);

    m_gizmoBatch.SetProjection(viewProj.x);
    m_gizmoBatch.LoadIdentity();
    m_gizmoBatch.SetMode(gl::RenderPrimitive::LINES);

    for (const SceneOctreeNode* node : nodes)
    {
        float t = (float)node->depth / 6.f;
        if (t > 1.f) t = 1.f;
        gl::u8 r = (gl::u8)((1.f - t) * 255.f), g = (gl::u8)(t * 255.f);
        Vec3 c = node->bounds.center();
        Vec3 sz = node->bounds.max - node->bounds.min;
        m_gizmoBatch.SetColor(r, g, 60, 220);
        m_gizmoBatch.CubeWire(c.x, c.y, c.z, sz.x, sz.y, sz.z);
    }

    m_gizmoBatch.Render();
    // gl::Renderer::SetDepthTest(true);
    // gl::Renderer::SetDepthWrite(true);
}

void SceneRenderer::draw_debug_views(int viewport_w, int viewport_h)
{
    if (m_waters.empty()) return;
    WaterNode* w = m_waters[0];
    if (!w->reflection_tex().IsValid()) return;

    gl::Renderer::SetDepthTest(false);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    m_debug.Bind();
    m_debug.SetVec2(m_locDTargetSize, (float)viewport_w, (float)viewport_h);

    const float dw = viewport_w * 0.28f, dh = viewport_h * 0.28f, pad = 12.f;
    w->reflection_tex().Bind(0);
    m_debug.SetVec4(m_locDRect, pad, pad, dw, dh);
    gl::Renderer::DrawQuad();
    w->refraction_tex().Bind(0);
    m_debug.SetVec4(m_locDRect, viewport_w - dw - pad, pad, dw, dh);
    gl::Renderer::DrawQuad();
    gl::Renderer::SetDepthTest(true);
}
