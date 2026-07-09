#pragma once

// All built-in GLSL of the scene renderer, in one place. Every body is
// version-less: SceneRenderer prepends gl::Renderer::ShaderHeader(stage),
// which supplies the GLSL version, precision qualifiers and the clip-plane
// macros (CLIP_VARYING / CLIP_SETUP / CLIP_APPLY) for the platform.

// ── built-in forward shader (lambert + ambient, diffuse map, clip plane) ──
// All shader bodies are version-less: Renderer::ShaderHeader() is prepended
// at init, providing the right GLSL version, precision and clip macros for
// the platform (desktop GL / ES / WebGL2).
static const char* kFwdVS = R"(
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform mat4 u_view;
uniform vec4 u_clipPlane;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_worldPos;
out float v_viewDepth;
CLIP_VARYING
void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    v_normal = normalize(mat3(u_model) * a_normal);
    v_uv = a_uv;
    v_viewDepth = -(u_view * worldPos).z; // positive distance ahead of the eye
    CLIP_SETUP(dot(worldPos, u_clipPlane));
    gl_Position = u_viewProj * worldPos;
}
)";

// Blinn-phong + CSM. Occlusion sampling: Poisson-disk PCF on the two near
// cascades (soft, no banding up close), 5x5 grid PCF on the far ones
// (cheaper where nobody can tell). Slope-scaled bias grows per cascade;
// the fragment is offset along its normal before the light-space transform;
// adjacent cascades cross-fade over the last 15% of each split so the
// transition line never shows.
static const char* kFwdFS = R"(
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_worldPos;
in float v_viewDepth;
out vec4 OutColor;
uniform vec3 u_baseColor;
uniform vec3 u_lightDir; // direction the light travels (sun -> ground)
uniform float u_unlit;
uniform sampler2D u_diffuse;
uniform vec3 u_cameraPos;
uniform vec2 u_specular; // x = strength, y = shininess
uniform sampler2DArray u_shadowMap;
uniform mat4 u_lightViewProj[4];
uniform float u_splits[4]; // far edge of each cascade, in view depth
uniform int u_cascadeCount; // 0 = shadows off
uniform int u_showCascades;
uniform vec2 u_shadowMapSize;

// ── local lights (point/spot), up to 4 of each ──
uniform int u_pointCount;
uniform vec4 u_pointPosRange[4]; // xyz position, w range
uniform vec4 u_pointColor[4];    // rgb premultiplied by intensity, w shadow slot (-1 none)
uniform samplerCube u_pointShadow0;
uniform samplerCube u_pointShadow1;
uniform int u_spotCount;
uniform vec4 u_spotPosRange[4];  // xyz position, w range
uniform vec4 u_spotDirInner[4];  // xyz direction, w cos(inner half-angle)
uniform vec4 u_spotColorOuter[4]; // rgb premultiplied, w cos(outer half-angle)
uniform float u_spotShadowSlot[4]; // -1 none, else 0/1
uniform mat4 u_spotShadowMat[2];
uniform sampler2D u_spotShadow0;
uniform sampler2D u_spotShadow1;
CLIP_VARYING

const vec2 kPoisson[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));

float shadowSample(int layer, vec2 uv)
{
    return texture(u_shadowMap, vec3(uv, float(layer))).r;
}

float shadowBias(int layer, float baseBias, float slopeBias)
{
    float ndl = max(dot(normalize(v_normal), -u_lightDir), 0.0);
    return (baseBias + slopeBias * (1.0 - ndl)) * (1.0 + float(layer) * 0.2);
}

// near cascades: 16-tap Poisson disk
float shadowPoisson(int layer, vec3 p)
{
    float bias = shadowBias(layer, 0.0005, 0.0006);
    vec2 texel = 1.0 / u_shadowMapSize;
    float occ = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        float d = shadowSample(layer, p.xy + kPoisson[i] * texel * 1.4);
        occ += (p.z - bias) > d ? 1.0 : 0.0;
    }
    return occ / 16.0;
}

// far cascades: 5x5 grid PCF
float shadowGrid(int layer, vec3 p)
{
    float bias = shadowBias(layer, 0.0008, 0.0006);
    vec2 texel = 1.0 / u_shadowMapSize;
    float occ = 0.0;
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y)
            occ += (p.z - bias) > shadowSample(layer, p.xy + vec2(x, y) * texel) ? 1.0 : 0.0;
    return occ / 25.0;
}

// 0 = fully lit, 1 = fully shadowed
float shadowOcclusion(int layer)
{
    // normal offset scales with the cascade (coarser texels farther away)
    vec3 offsetPos = v_worldPos + normalize(v_normal) * (0.05 + 0.05 * float(layer));
    vec4 lp = u_lightViewProj[layer] * vec4(offsetPos, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 0.0;
    if (layer <= 1) return shadowPoisson(layer, p);
    return shadowGrid(layer, p);
}

// point-light shadow: linear-distance cubemap, 20-direction disk PCF
const vec3 kCubeDirs[20] = vec3[](
    vec3(1, 1, 1), vec3(1, -1, 1), vec3(-1, -1, 1), vec3(-1, 1, 1),
    vec3(1, 1, -1), vec3(1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
    vec3(1, 1, 0), vec3(1, -1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
    vec3(1, 0, 1), vec3(-1, 0, 1), vec3(1, 0, -1), vec3(-1, 0, -1),
    vec3(0, 1, 1), vec3(0, -1, 1), vec3(0, -1, -1), vec3(0, 1, -1));

float pointCubeSample(int slot, vec3 dir)
{
    if (slot == 0) return texture(u_pointShadow0, dir).r;
    return texture(u_pointShadow1, dir).r;
}

// 1 = lit, 0 = shadowed. The map stores length(frag - light) / range.
float pointShadowFactor(int slot, vec3 lightToFrag, float range)
{
    float current = (length(lightToFrag) - 0.15) / range;
    vec3 dir = normalize(lightToFrag);
    float lit = 0.0;
    for (int i = 0; i < 20; ++i)
        lit += current > pointCubeSample(slot, dir + kCubeDirs[i] * 0.03) ? 0.0 : 1.0;
    return lit / 20.0;
}

float spotMapSample(int slot, vec2 uv)
{
    if (slot == 0) return texture(u_spotShadow0, uv).r;
    return texture(u_spotShadow1, uv).r;
}

// 1 = lit, 0 = shadowed. Projective map + 3x3 PCF.
float spotShadowFactor(int slot, vec3 worldPos)
{
    vec4 lp = u_spotShadowMat[slot] * vec4(worldPos + normalize(v_normal) * 0.02, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    vec2 texel =
        1.0 / vec2(slot == 0 ? textureSize(u_spotShadow0, 0) : textureSize(u_spotShadow1, 0));
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            lit += (p.z - 0.0015) > spotMapSample(slot, p.xy + vec2(x, y) * texel) ? 0.0 : 1.0;
    return lit / 9.0;
}

void main()
{
    CLIP_APPLY;
    vec3 albedo = texture(u_diffuse, v_uv).rgb * u_baseColor;

    int layer = 0;
    float occlusion = 0.0;
    if (u_cascadeCount > 0)
    {
        layer = u_cascadeCount - 1;
        for (int i = 0; i < u_cascadeCount; ++i)
        {
            if (v_viewDepth < u_splits[i])
            {
                layer = i;
                break;
            }
        }
        // cross-fade into the next cascade over the last 15% of this one
        if (layer < u_cascadeCount - 1)
        {
            float edge = u_splits[layer];
            float blend = smoothstep(edge * 0.85, edge, v_viewDepth);
            occlusion = (blend > 0.001)
                            ? mix(shadowOcclusion(layer), shadowOcclusion(layer + 1), blend)
                            : shadowOcclusion(layer);
        }
        else
        {
            occlusion = shadowOcclusion(layer);
        }
    }

    vec3 n = normalize(v_normal);
    vec3 toLight = -u_lightDir;
    float diffuse = max(dot(n, toLight), 0.0);
    vec3 viewDir = normalize(u_cameraPos - v_worldPos);
    float spec = pow(max(dot(n, normalize(toLight + viewDir)), 0.0), u_specular.y) * u_specular.x;

    float lit = 1.0 - occlusion;
    vec3 color = albedo * (0.30 + 0.70 * diffuse * lit) + vec3(spec * lit);

    // ── local lights ──
    for (int i = 0; i < u_pointCount; ++i)
    {
        vec3 toL = u_pointPosRange[i].xyz - v_worldPos;
        float dist = length(toL);
        float range = u_pointPosRange[i].w;
        if (dist >= range) continue;
        float att = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
        att = att * att / (dist * dist + 1.0);
        vec3 L = toL / dist;
        float ndl = max(dot(n, L), 0.0);
        if (ndl <= 0.0 || att <= 0.0) continue;
        float sh = 1.0;
        int slot = int(u_pointColor[i].w);
        if (slot >= 0) sh = pointShadowFactor(slot, -toL, range);
        float sp = pow(max(dot(n, normalize(L + viewDir)), 0.0), u_specular.y) * u_specular.x;
        color += u_pointColor[i].rgb * (albedo * ndl + vec3(sp)) * att * sh;
    }
    for (int i = 0; i < u_spotCount; ++i)
    {
        vec3 toL = u_spotPosRange[i].xyz - v_worldPos;
        float dist = length(toL);
        float range = u_spotPosRange[i].w;
        if (dist >= range) continue;
        vec3 L = toL / dist;
        // cone falloff between the outer and inner cosines, squared
        float cosAng = dot(L, -u_spotDirInner[i].xyz);
        float cone = clamp((cosAng - u_spotColorOuter[i].w) /
                               (u_spotDirInner[i].w - u_spotColorOuter[i].w),
                           0.0, 1.0);
        cone *= cone;
        if (cone <= 0.0) continue;
        float att = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
        att = att * att / (dist * dist + 1.0);
        float ndl = max(dot(n, L), 0.0);
        if (ndl <= 0.0) continue;
        float sh = 1.0;
        int slot = int(u_spotShadowSlot[i]);
        if (slot >= 0) sh = spotShadowFactor(slot, v_worldPos);
        float sp = pow(max(dot(n, normalize(L + viewDir)), 0.0), u_specular.y) * u_specular.x;
        color += u_spotColorOuter[i].rgb * (albedo * ndl + vec3(sp)) * att * cone * sh;
    }

    color = mix(color, albedo, u_unlit);

    if (u_showCascades != 0 && u_cascadeCount > 0)
    {
        const vec3 kTint[4] = vec3[](vec3(1.0, 0.6, 0.6), vec3(0.6, 1.0, 0.6),
                                     vec3(0.6, 0.6, 1.0), vec3(1.0, 1.0, 0.6));
        color *= kTint[layer];
    }
    OutColor = vec4(color, 1.0);
}
)";

// ── shadow depth pass: geometry into one cascade layer, no color ──
static const char* kDepthVS = R"(
layout(location = 0) in vec3 a_position;
uniform mat4 u_lightMVP;
void main()
{
    gl_Position = u_lightMVP * vec4(a_position, 1.0);
}
)";

static const char* kDepthFS = R"(
void main()
{
}
)";

// ── point-light depth pass: one cube face per view, the depth value is the
// LINEAR distance to the light divided by its range (what the forward pass
// compares against) ──
static const char* kPointDepthVS = R"(
layout(location = 0) in vec3 a_position;
uniform mat4 u_model;
uniform mat4 u_lightVP;
out vec3 v_worldPos;
void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    gl_Position = u_lightVP * worldPos;
}
)";

static const char* kPointDepthFS = R"(
in vec3 v_worldPos;
uniform vec3 u_lightPos;
uniform float u_range;
void main()
{
    gl_FragDepth = length(v_worldPos - u_lightPos) / u_range;
}
)";

// ── water surface shader: projective reflection/refraction + fresnel ──
static const char* kWaterVS = R"(
layout(location = 0) in vec3 a_position;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform float u_tiling;
out vec4 v_clip;
out vec2 v_uv;
out vec3 v_world;
void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_world = worldPos.xyz;
    v_uv = a_uv * u_tiling;
    v_clip = u_viewProj * worldPos;
    gl_Position = v_clip;
}
)";

static const char* kWaterFS = R"(
in vec4 v_clip;
in vec2 v_uv;
in vec3 v_world;
out vec4 OutColor;
uniform sampler2D u_reflection;
uniform sampler2D u_refraction;
uniform sampler2D u_refractionDepth;
uniform vec3 u_cameraPos;
uniform vec3 u_lightDir;
uniform vec3 u_waterColor;
uniform float u_time;
uniform float u_distortion;
uniform float u_colorMix;
uniform vec2 u_camPlanes; // near, far — to linearize depth

float linearDepth(float d)
{
    float z = d * 2.0 - 1.0;
    return 2.0 * u_camPlanes.x * u_camPlanes.y /
           (u_camPlanes.y + u_camPlanes.x - z * (u_camPlanes.y - u_camPlanes.x));
}

void main()
{
    vec2 ndc = (v_clip.xy / v_clip.w) * 0.5 + 0.5;

    // per-pixel water depth: distance from the surface down to whatever the
    // refraction view saw at this pixel
    float floorDist = linearDepth(texture(u_refractionDepth, ndc).r);
    float waterDist = linearDepth(gl_FragCoord.z);
    float waterDepth = floorDist - waterDist;
    // shoreline factor: 0 right at the shore, 1 in open water
    float soft = clamp(waterDepth / 1.5, 0.0, 1.0);

    // procedural dudv: two scrolling wave fields, calmed near the shore
    float t = u_time;
    vec2 d1 = vec2(sin(v_uv.y * 12.3 + t * 4.0), cos(v_uv.x * 11.7 + t * 3.4));
    vec2 d2 = vec2(sin((v_uv.x + v_uv.y) * 9.1 - t * 2.7),
                   cos((v_uv.y - v_uv.x) * 10.3 + t * 3.9));
    vec2 distortion = (d1 + d2) * 0.25 * u_distortion * soft;

    vec2 reflUV = clamp(vec2(ndc.x, 1.0 - ndc.y) + distortion, 0.001, 0.999);
    vec2 refrUV = clamp(ndc + distortion, 0.001, 0.999);
    vec3 refl = texture(u_reflection, reflUV).rgb;
    vec3 refr = texture(u_refraction, refrUV).rgb;

    // fresnel: looking straight down favors refraction, grazing favors reflection
    vec3 viewDir = normalize(u_cameraPos - v_world);
    float fresnel = pow(clamp(dot(viewDir, vec3(0.0, 1.0, 0.0)), 0.0, 1.0), 0.7);

    vec3 color = mix(refl, refr, fresnel);
    // tint grows with depth: shallow water stays clear, deep water murks up
    float murk = clamp(u_colorMix + waterDepth * 0.02, 0.0, 0.75);
    color = mix(color, u_waterColor, murk);

    // sun glint from the distorted surface normal, faded out at the shore
    vec3 n = normalize(vec3(distortion.x * 8.0, 1.0, distortion.y * 8.0));
    vec3 h = normalize(viewDir - u_lightDir);
    color += vec3(0.5 * pow(max(dot(n, h), 0.0), 120.0) * soft);

    // soft edge: the surface fades to nothing where it meets the terrain
    OutColor = vec4(color, clamp(waterDepth / 0.9, 0.0, 1.0));
}
)";

// debug overlay: shows an extra view's texture in a corner rect
static const char* kDebugFS = R"(
in vec2 v_uv;
out vec4 OutColor;
uniform sampler2D u_tex;
void main()
{
    // shown raw (no vertical flip): a reflection view reads upside down,
    // which makes it obvious the overlay is the extra view, not the scene
    OutColor = vec4(texture(u_tex, v_uv).rgb, 1.0);
}
)";
