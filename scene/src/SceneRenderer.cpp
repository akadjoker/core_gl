#include <chrono>
#include "scene/SceneRenderer.hpp"
#include "scene/Camera3D.hpp"
#include "scene/LightNode.hpp"
#include "scene/Material.hpp"
#include "scene/DecalSystemNode.hpp"
#include "scene/GrassSystemNode.hpp"
#include "scene/OceanNode.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/RibbonTrailNode.hpp"
#include "scene/TerrainPagingNode.hpp"
#include "scene/LensFlareNode.hpp"
#include "scene/Camera3D.hpp"
#include "scene/Pixmap.hpp"
#include "scene/WaterNode.hpp"
#include <coregl/gl_framebuffer.hpp>
#include <coregl/gl_renderer.hpp>
#include <coregl/gl_vertex_array.hpp>
#include <cstdio>

#include "SceneShaders.hpp"

// ── CSM cascade fitting ──────────────────────────────────────────────────

// split distances: log/uniform blend (finer slices near the camera)
static void csmSplits(float nearClip, float farClip, float* splits, int numCascades)
{
    float logBase = logf(farClip / nearClip) / (float)numCascades;
    float uniformStep = 1.0f / (float)numCascades;
    splits[0] = nearClip;
    for (int i = 1; i <= numCascades; ++i)
    {
        float logSplit = nearClip * expf(logBase * (float)i);
        float unifSplit = nearClip + uniformStep * (float)i * (farClip - nearClip);
        float blend = (float)i / (float)numCascades;
        splits[i] = logSplit + (unifSplit - logSplit) * (blend * 0.5f);
    }
}

// one cascade's lightProjection * lightView:
// 1. unproject this slice's 8 frustum corners to world space
// 2. light view looks at their center along the sun direction
// 3. tight light-space AABB, Z range extended so casters outside the slice
//    (toward the sun, or just behind the camera) still land in the map
// 4. AABB snapped to shadow-texel steps: the ortho window moves in whole
//    texels as the camera moves, so shadow edges don't shimmer
static Mat4 csmCascadeMatrix(int cascade, float aspect, float fovDeg, const Mat4& view,
                             const float* splits, const Vec3& lightDir, float shadowMapSize)
{
    Mat4 projection = Mat4::Perspective((double)fovDeg, (double)aspect, (double)splits[cascade],
                                        (double)splits[cascade + 1]);
    Mat4 invViewProj = Mat4::Inverse(projection * view);

    Vec4 corners[8] = {
        Vec4(-1.f, -1.f, -1.f, 1.f), Vec4(-1.f, -1.f, 1.f, 1.f), Vec4(-1.f, 1.f, -1.f, 1.f),
        Vec4(-1.f, 1.f, 1.f, 1.f),   Vec4(1.f, -1.f, -1.f, 1.f), Vec4(1.f, -1.f, 1.f, 1.f),
        Vec4(1.f, 1.f, -1.f, 1.f),   Vec4(1.f, 1.f, 1.f, 1.f),
    };
    for (int i = 0; i < 8; ++i)
    {
        Vec4 world = invViewProj * corners[i];
        if (fabsf(world.w) > 1e-6f) corners[i] = world / world.w;
    }

    Vec3 center(0.f, 0.f, 0.f);
    for (int i = 0; i < 8; ++i)
        center += Vec3(corners[i].x, corners[i].y, corners[i].z);
    center *= (1.0f / 8.0f);

    // tilted up vector avoids a singular basis when the sun is vertical
    Vec3 eye = center - lightDir;
    Mat4 lightView = Mat4::LookAt(eye, center, Vec3(0.001f, 1.f, 0.001f));

    Vec3 minV(1e30f, 1e30f, 1e30f);
    Vec3 maxV(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; ++i)
    {
        Vec3 t = lightView * Vec3(corners[i].x, corners[i].y, corners[i].z);
        minV = minV.Min(t);
        maxV = maxV.Max(t);
    }

    const float depthScale = 5.0f;
    minV.z *= (minV.z < 0.f) ? depthScale : (1.0f / depthScale);
    maxV.z *= (maxV.z > 0.f) ? depthScale : (1.0f / depthScale);

    float texelX = (maxV.x - minV.x) / shadowMapSize;
    float texelY = (maxV.y - minV.y) / shadowMapSize;
    if (texelX < 1e-6f) texelX = 1e-6f;
    if (texelY < 1e-6f) texelY = 1e-6f;
    minV.x = floorf(minV.x / texelX) * texelX;
    minV.y = floorf(minV.y / texelY) * texelY;
    maxV.x = floorf(maxV.x / texelX) * texelX;
    maxV.y = floorf(maxV.y / texelY) * texelY;

    Mat4 lightProj = Mat4::Ortho(minV.x, maxV.x, minV.y, maxV.y, -maxV.z, -minV.z);
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
    if (!loadStage(m_water, gl::PipelineStage::VERTEX, kWaterVS) ||
        !loadStage(m_water, gl::PipelineStage::FRAGMENT, kWaterFS) || !m_water.Link())
        return false;

    m_locModel = m_forward.GetLocation("u_model");
    m_locViewProj = m_forward.GetLocation("u_viewProj");
    m_locView = m_forward.GetLocation("u_view");
    m_locColor = m_forward.GetLocation("u_baseColor");
    m_locLightDir = m_forward.GetLocation("u_lightDir");
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

    if (!m_debug.LoadFromString(gl::PipelineStage::VERTEX, gl::Renderer::QuadShaderSource()) ||
        !loadStage(m_debug, gl::PipelineStage::FRAGMENT, kDebugFS) || !m_debug.Link())
        return false;
    m_locDRect = m_debug.GetLocation("u_rect");
    m_locDTargetSize = m_debug.GetLocation("u_targetSize");
    m_debug.Bind();
    m_debug.SetInt("u_tex", 0);

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

    m_items.reserve(256);
    m_ready = true;
    return true;
}

void SceneRenderer::release()
{
    m_forward.Release();
    m_water.Release();
    m_oceanShader.Release();
    m_sky.Release();
    m_particle.Release();
    m_grass.Release();
    m_terrainShader.Release();
    m_tonemap.Release();
    m_godray.Release();
    if (m_statsBatchReady) m_statsBatch.Release();
    m_statsBatchReady = false;
    m_hdrFbo.Release();
    m_pingFbo.Release();
    m_hdrColor.Release();
    m_hdrDepth.Release();
    m_pingColor.Release();
    m_postW = m_postH = 0;
    m_post_enabled = false;
    m_debug.Release();
    m_depth.Release();
    m_pointDepth.Release();
    m_shadowTex.Release();
    m_shadowFbo.Release();
    m_white.Release();
    m_gray.Release();
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

bool SceneRenderer::enable_post(bool godrays)
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

    m_post_enabled = true;
    m_godrays_enabled = godrays;
    return true;
}

// (re)creates the HDR targets when the viewport size changes
bool SceneRenderer::ensure_post_targets(int w, int h)
{
    if (m_postW == w && m_postH == h) return true;

    m_hdrColor.Release();
    m_hdrDepth.Release();
    m_pingColor.Release();
    m_hdrFbo.Release();
    m_pingFbo.Release();

    m_hdrColor.Load2D(nullptr, w, h, gl::TextureFormat::RGBA16F);
    m_hdrColor.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    m_hdrColor.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_hdrDepth.LoadDepth(w, h, gl::TextureFormat::DEPTH24);
    m_hdrFbo.AttachTexture(m_hdrColor, gl::Attachment::COLOR0);
    m_hdrFbo.AttachTexture(m_hdrDepth, gl::Attachment::DEPTH);
    m_hdrFbo.SetDrawBuffers();
    if (!m_hdrFbo.IsComplete()) return false;

    m_pingColor.Load2D(nullptr, w, h, gl::TextureFormat::RGBA16F);
    m_pingColor.SetFilter(gl::TextureFilter::LINEAR, gl::TextureFilter::LINEAR);
    m_pingColor.SetWrap(gl::TextureWrap::CLAMP_TO_EDGE, gl::TextureWrap::CLAMP_TO_EDGE);
    m_pingFbo.AttachTexture(m_pingColor, gl::Attachment::COLOR0);
    m_pingFbo.SetDrawBuffers();
    if (!m_pingFbo.IsComplete()) return false;

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
        m_cascadeMat[c] = csmCascadeMatrix(c, camera->get_aspect(), camera->get_fov(), view,
                                           m_splits, m_lightDir, (float)m_shadowSize);

    m_shadowFbo.Bind();
    gl::Renderer::Viewport(0, 0, m_shadowSize, m_shadowSize);
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetCull(gl::CullMode::NONE);
    gl::Renderer::SetPolygonOffset(true, 2.5f, 4.f);

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
        scene.collect(m_shadow_items, &cascadeFrustum);

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
    }
    gl::Renderer::SetPolygonOffset(false);
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
    m_shadow_items.clear();
    scene.collect(m_shadow_items);

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
                Mat4 vp = proj * Mat4::LookAt(pos, pos + kFaceDir[face], kFaceUp[face]);
                m_pointDepth.SetMat4(m_locPDLightVP, vp.x);
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
            gl::Renderer::SetPolygonOffset(true, 2.f, 3.f);

            const Vec3 dir = spot->direction();
            const float fovDeg = spot->outer_angle * 2.f * 57.29578f;
            // the near plane must scale with the range: a tiny near crams
            // all usable depth into the last few thousandths of the map and
            // the compare bias then covers the whole scene (no shadow)
            const float nearPlane = spot->range * 0.05f;
            Mat4 vp =
                Mat4::Perspective((double)fovDeg, 1.0, (double)nearPlane, (double)spot->range) *
                Mat4::LookAt(pos, pos + dir, Vec3(0.001f, 1.f, 0.001f));
            m_spotShadowMat[spotSlot] = vp;

            m_depth.Bind();
            for (const RenderItem& item : m_shadow_items)
            {
                Mat4 mvp = vp * item.world;
                m_depth.SetMat4(m_locDepthMVP, mvp.x);
                item.vao->Bind();
                gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, item.index_count,
                                          item.first_index);
            }
            gl::Renderer::SetPolygonOffset(false);
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

void SceneRenderer::collect_ribbontrails(Node* node, std::vector<RibbonTrailNode*>& out)
{
    RibbonTrailNode* r = node->as<RibbonTrailNode>();
    if (r) out.push_back(r);
    for (Node* child : node->get_children())
        collect_ribbontrails(child, out);
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
    m_terrainShader.SetVec3(m_locTAmbient, 0.30f, 0.30f, 0.32f);
    m_terrainShader.SetVec4(m_locTClipPlane, v.clip_plane.x, v.clip_plane.y, v.clip_plane.z,
                            v.clip_plane.w);

    // same CSM the forward pass uses; the terrain shader samples it with
    // the identical occlusion functions
    m_terrainShader.SetInt("u_cascadeCount", m_cascades);
    if (m_cascades > 0)
    {
        m_shadowTex.Bind(10);
        m_terrainShader.SetVec2("u_shadowMapSize", (float)m_shadowSize, (float)m_shadowSize);
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

        // per-node linear fog (end <= 0 disables in the shader)
        if (t->fog_enabled())
        {
            const Vec3& fc = t->fog_color();
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
    scene.collect(m_items, &frustum);
    m_last_items = (int)m_items.size();

    if (v.target)
        v.target->Bind();
    else
        gl::Renderer::BindScreen();
    gl::Renderer::Viewport(0, 0, v.w, v.h);
    gl::Renderer::SetDepthTest(true);
    gl::Renderer::SetDepthWrite(true);
    gl::Renderer::SetCull(gl::CullMode::BACK);
    // a mirrored view reverses triangle winding: flip what counts as front
    gl::Renderer::SetFrontFaceCW(v.mirrored);
    gl::Renderer::SetClipDistance(0, v.use_clip);
    gl::Renderer::ClearColor(m_clearColor.x, m_clearColor.y, m_clearColor.z, 1.0f);
    gl::Renderer::Clear(true, true);

    m_forward.Bind();
    Mat4 vp = v.proj * v.view;
    m_forward.SetMat4(m_locViewProj, vp.x);
    m_forward.SetMat4(m_locView, v.view.x);
    m_forward.SetVec3(m_locLightDir, m_lightDir.x, m_lightDir.y, m_lightDir.z);
    m_forward.SetVec3(m_locCameraPos, v.cam_pos.x, v.cam_pos.y, v.cam_pos.z);
    m_forward.SetVec4(m_locClipPlane, v.clip_plane.x, v.clip_plane.y, v.clip_plane.z,
                      v.clip_plane.w);

    set_light_uniforms();

    m_forward.SetInt(m_locCascadeCount, m_cascades);
    if (m_cascades > 0)
    {
        m_shadowTex.Bind(1);
        m_forward.SetInt(m_locShowCascades, m_show_cascades ? 1 : 0);
        m_forward.SetVec2(m_locShadowSize, (float)m_shadowSize, (float)m_shadowSize);
        for (int i = 0; i < m_cascades; ++i)
        {
            m_forward.SetMat4(m_locCascadeMat0 + i, m_cascadeMat[i].x);
            m_forward.SetFloat(m_locSplits0 + i, m_splits[i + 1]);
        }
    }

    for (const RenderItem& item : m_items)
    {
        const Material* mat = item.material;
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
        m_forward.SetMat4(m_locModel, item.world.x);
        item.vao->Bind();
        gl::Renderer::DrawIndexed(gl::RenderPrimitive::TRIANGLES, item.index_count,
                                  item.first_index);
    }

    // splat-mode paged terrain: opaque, in-view like any item (reflections
    // and refraction included, clipped by the view's plane)
    if (!m_pagedTerrains.empty()) draw_paged_terrain(v, frustum);

    if (m_sky_enabled)
    {
        // background: fills every pixel the opaque pass left at depth 1.0.
        // The sky shader writes no clip distance, so the hardware plane
        // must be off while it draws.
        gl::Renderer::SetClipDistance(0, false);
        gl::Renderer::SetDepthWrite(false);
        gl::Renderer::SetDepthFunction(gl::DepthFunction::LEQUAL);
        gl::Renderer::SetCull(gl::CullMode::NONE);
        m_sky.Bind();
        Mat4 invViewProj = Mat4::Inverse(vp);
        m_sky.SetMat4("u_invViewProj", invViewProj.x);
        m_sky.SetVec3("u_cameraPos", v.cam_pos.x, v.cam_pos.y, v.cam_pos.z);
        m_sky.SetVec3("u_sunDir", -m_lightDir.x, -m_lightDir.y, -m_lightDir.z);
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
    if (o->bump) o->bump->Bind(3);
    if (o->foam) o->foam->Bind(4);

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
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA,
                                  gl::BlendFactor::ONE_MINUS_SRC_ALPHA);

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

void SceneRenderer::render(Scene& scene, int viewport_w, int viewport_h)
{
    if (!m_ready) return;
    Camera3D* camera = scene.get_active_camera();
    if (!camera) return; // nothing to see with

    camera->set_viewport_size(viewport_w, viewport_h);
    Mat4 proj = camera->get_projection_matrix();
    Mat4 view = camera->get_view_matrix();
    Vec3 cameraPos = camera->get_global_position();

    // collected before the shadow pass: splat pages cast shadows too
    m_pagedTerrains.clear();
    collect_paged_terrain(&scene.root(), m_pagedTerrains);

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

    // ── main view (into the HDR target when post-processing is on) ──
    bool post = m_post_enabled && ensure_post_targets(viewport_w, viewport_h);
    RenderView main_view;
    main_view.view = view;
    main_view.proj = proj;
    main_view.target = post ? &m_hdrFbo : nullptr;
    main_view.w = viewport_w;
    main_view.h = viewport_h;
    main_view.cam_pos = cameraPos;
    draw_view(scene, main_view);

    m_grassSystems.clear();
    collect_grass(&scene.root(), m_grassSystems);
    if (!m_grassSystems.empty()) draw_grass(proj * view);


    // water surfaces draw last, depth-tested against the scene (they land
    // in whatever target the main view used)
    if (!m_waters.empty())
        draw_water_surfaces(view, proj, cameraPos, camera->get_near(), camera->get_far());

    // particles: billboard toward the active camera, then draw over
    // everything opaque/water already in this target
    m_particleSystems.clear();
    collect_particles(&scene.root(), m_particleSystems);
    m_decalSystems.clear();
    collect_decals(&scene.root(), m_decalSystems);
    m_ribbonTrails.clear();
    collect_ribbontrails(&scene.root(), m_ribbonTrails);
    if (!m_particleSystems.empty() || !m_decalSystems.empty() || !m_ribbonTrails.empty())
    {
        Vec3 camRight = Mat4(camera->get_global_rotation()) * Vec3(1.f, 0.f, 0.f);
        Vec3 camUp = Mat4(camera->get_global_rotation()) * Vec3(0.f, 1.f, 0.f);
        for (ParticleSystemNode* ps : m_particleSystems)
            ps->build_billboards(camRight, camUp);
        for (RibbonTrailNode* rt : m_ribbonTrails)
            rt->rebuild(cameraPos, camUp);
        draw_particles(proj * view);
        draw_ribbontrails(proj * view);
    }

    // lens flares: screen-space, additive, on top of everything
    m_lensflares.clear();
    collect_lensflares(&scene.root(), m_lensflares);
    if (!m_lensflares.empty()) draw_lensflares(*camera);

    if (post)
    {
        gl::Renderer::SetDepthTest(false);
        gl::Renderer::SetCull(gl::CullMode::NONE);

        const gl::Texture* colorSrc = &m_hdrColor;
        if (m_godrays_enabled && m_cascades > 0)
        {
            // volumetric sun: march the LAST cascade through the scene depth
            m_pingFbo.Bind();
            gl::Renderer::Viewport(0, 0, viewport_w, viewport_h);
            m_godray.Bind();
            m_hdrColor.Bind(0);
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
            colorSrc = &m_pingColor;
        }

        // filmic tonemap to the screen
        gl::Renderer::BindScreen();
        gl::Renderer::Viewport(0, 0, viewport_w, viewport_h);
        m_tonemap.Bind();
        const_cast<gl::Texture*>(colorSrc)->Bind(0);
        m_tonemap.SetFloat("u_exposure", m_exposure);
        gl::Renderer::DrawFullscreenTriangle();
        gl::Renderer::SetDepthTest(true);
    }

    if (m_debug_views) draw_debug_views(viewport_w, viewport_h);

    // snapshot BEFORE the stats panel draws, so the panel reports the
    // scene's cost, not its own
    m_frameStats = gl::Renderer::GetStats();

    // frame clock: time between render() calls, smoothed for a stable readout
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const gl::u64 ns = (gl::u64)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
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
    gl::Renderer::SetBlendFactors(gl::BlendFactor::SRC_ALPHA,
                                  gl::BlendFactor::ONE_MINUS_SRC_ALPHA);

    m_statsBatch.SetColor(10, 14, 20, 190);
    m_statsBatch.Rect(8.f, 8.f, wMax + pad * 2.f, (float)n * lineH + pad * 2.f);
    m_statsBatch.SetColor(140, 235, 140, 255);
    for (int i = 0; i < n; ++i)
        m_statsBatch.Text(8.f + pad, 8.f + pad + (float)i * lineH, ts, lines[i]);
    m_statsBatch.Render();

    gl::Renderer::SetBlend(false);
    gl::Renderer::SetDepthTest(true);
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
