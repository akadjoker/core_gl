#version 300 es
precision highp float;

out vec4 FragColor;

in vec4 ClipSpace;
in vec2 TexCoord;
in vec3 ToCameraVector;
in vec3 WorldPos;
in vec3 v_normal;
in vec2 v_bumpMapTexCoord;

uniform sampler2D reflectionTexture;
uniform sampler2D refractionTexture;
uniform sampler2D refractionDepth;
uniform sampler2D waterBump;
uniform sampler2D foamTexture;

uniform vec3 u_cameraPosition;
uniform float u_waveHeight;
uniform vec4 u_waterColor;
uniform float u_colorBlendFactor;
uniform float u_time;
uniform float mult;

 
uniform float u_foamRange;      // Distância da borda (substitui foamEdgeDistance)
uniform float u_foamScale;      // Escala da textura
uniform float u_foamSpeed;      // Velocidade da animação
uniform float u_foamIntensity;  // Intensidade geral (0-1)

const float foamCutoff = 0.5;   

void main() 
{

    float distToCamera = length(u_cameraPosition - WorldPos);
    float lodFactor = smoothstep(50.0, 100.0, distToCamera);


    // Bump distortion
    vec4 bumpColor = texture(waterBump, v_bumpMapTexCoord);
    vec2 perturbation = u_waveHeight * (bumpColor.rg - 0.5);
    perturbation *= (1.0 - lodFactor * 0.5);
    
    // NDC coordinates
    vec2 ndc = (ClipSpace.xy / ClipSpace.w) * 0.5 + 0.5;
    vec2 reflectTexCoords = vec2(ndc.x, 1.0 - ndc.y);
    vec2 refractTexCoords = vec2(ndc.x, ndc.y);
    
    // Depth calculation
    float near = 0.1;
    float far = 1000.0;
    
    float depth = texture(refractionDepth, refractTexCoords).r;
    float floorDistance = 2.0 * near * far / (far + near - (2.0 * depth - 1.0) * (far - near));
    
    depth = gl_FragCoord.z;
    float waterDistance = 2.0 * near * far / (far + near - (2.0 * depth - 1.0) * (far - near));
    float waterDepth = floorDistance - waterDistance;
    if (waterDepth < 0.0) 
    {
         discard;
         return;
    }
    float normalizedDepth = clamp(waterDepth / mult, 0.0, 1.0);




 
    
    // Apply distortion
    reflectTexCoords = clamp(reflectTexCoords + perturbation, 0.001, 0.999);
    refractTexCoords = clamp(refractTexCoords + perturbation, 0.001, 0.999);
    
    vec4 reflectColor = texture(reflectionTexture, reflectTexCoords);
    vec4 refractColor = texture(refractionTexture, refractTexCoords);
    
    // Fresnel
    vec3 eyeVector = normalize(u_cameraPosition - WorldPos);
    vec3 upVector = vec3(0.0, 1.0, 0.0);
    float fresnelTerm = max(dot(eyeVector, upVector), 0.0);
    
    vec4 combinedColor = refractColor * fresnelTerm + reflectColor * (1.0 - fresnelTerm);
    vec4 finalColor = u_colorBlendFactor * u_waterColor + (1.0 - u_colorBlendFactor) * combinedColor;

 
        
    
    
    // === FOAM SYSTEM ===
    
    // 1. Edge foam (shore/shallow water)
    float edgeFoam = smoothstep(u_foamRange, 0.0, waterDepth);
    edgeFoam = pow(edgeFoam, 2.0);
    
    // 2. Foam texture com animação (2 camadas para mais detalhe)
    vec2 foamUV1 = WorldPos.xz * u_foamScale + vec2(u_time * u_foamSpeed, u_time * u_foamSpeed * 0.6);
    vec2 foamUV2 = WorldPos.xz * u_foamScale * 0.7 - vec2(u_time * u_foamSpeed * 0.8, u_time * u_foamSpeed);
    
    float foamPattern1 = texture(foamTexture, foamUV1).r;
    float foamPattern2 = texture(foamTexture, foamUV2).r;
    float foamPattern = (foamPattern1 + foamPattern2) * 0.5;
    
    // 3. Wave crest foam (baseado na normal)
    float waveHeight = 1.0 - v_normal.y;
    float crestFoam = smoothstep(0.4, 0.8, waveHeight) * 0.5;
    
    // 4. Combinar todos os tipos de foam
    float foamFactor = max(edgeFoam, crestFoam);
    foamFactor = foamFactor * smoothstep(foamCutoff - 0.1, foamCutoff + 0.1, foamPattern);
    foamFactor *= u_foamIntensity;   

    foamFactor *= (1.0 - lodFactor * 0.7);

    float foamDistanceFade = 1.0 - smoothstep(2.0, 50.0, distToCamera);
    foamFactor *= foamDistanceFade;
    
    // Cor do foam
    vec3 foamColor = vec3(0.95, 0.98, 1.0);
    
   finalColor.rgb = mix(finalColor.rgb, foamColor, foamFactor);
    
    
    FragColor = finalColor;
}
