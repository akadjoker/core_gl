#version 300 es
precision highp float;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in float Height;
in vec3 WorldPos;
 

out vec4 FragColor;
 
uniform sampler2D u_grassTexture;      // Baixo
uniform sampler2D u_dirtTexture;       // Médio baixo
uniform sampler2D u_rockTexture;       // Médio alto
uniform sampler2D u_snowTexture;       // Alto
uniform sampler2D u_detailMap;         // 1 único detail  

// ============================================
// UNIFORMS
// ============================================
uniform vec3 u_lightPos;
uniform vec3 u_viewPos;
uniform vec3 u_lightColor;

uniform float u_maxHeight;
uniform float u_minHeight;
uniform float u_textureScale;
uniform float u_detailScale;
uniform float u_detailStrength;

uniform float u_time;
uniform float u_waterLevel;          
uniform vec3 u_underwaterFogColor;   
uniform float u_underwaterFogDensity;

uniform vec4 clipPlane;
uniform bool useClipPlane;

// ============================================
// SMOOTH BLEND
// ============================================
float SmoothBlend(float value, float min, float max, float blendRange)
{
    if (value < min) return 0.0;
    if (value > max) return 0.0;
    
    float center = (min + max) * 0.5;
    
    if (value < center)
    {
        float t = (value - min) / blendRange;
        return smoothstep(0.0, 1.0, t);
    }
    else
    {
        float t = (max - value) / blendRange;
        return smoothstep(0.0, 1.0, t);
    }
}

void main()
{
 
    if(useClipPlane)
    {
     if (dot(vec4(FragPos, 1.0), clipPlane) < 0.0)
     {
      // discard;
     }
    }
   
    vec3 normal = normalize(Normal);
    
    // ============================================
    // 1. TEXTURE SPLATTING (baseado em altura)
    // ============================================
    float heightFactor = (Height - u_minHeight) / (u_maxHeight - u_minHeight);
    heightFactor = clamp(heightFactor, 0.0, 1.0);
    
    float blendRange = 0.15;
    
    // Calcular weights
    float grassWeight = SmoothBlend(heightFactor, 0.0, 0.4, blendRange);
    float dirtWeight = SmoothBlend(heightFactor, 0.3, 0.6, blendRange);
    float rockWeight = SmoothBlend(heightFactor, 0.5, 0.85, blendRange);
    float snowWeight = SmoothBlend(heightFactor, 0.75, 1.0, blendRange);
    
    // Normalizar weights
    float totalWeight = grassWeight + dirtWeight + rockWeight + snowWeight;
    if (totalWeight > 0.0)
    {
        grassWeight /= totalWeight;
        dirtWeight /= totalWeight;
        rockWeight /= totalWeight;
        snowWeight /= totalWeight;
    }
    
    // ============================================
    // 2. SAMPLE TEXTURAS BASE
    // ============================================
    vec3 grassColor = texture(u_grassTexture, TexCoord * u_textureScale).rgb;
    vec3 dirtColor = texture(u_dirtTexture, TexCoord * u_textureScale).rgb;
    vec3 rockColor = texture(u_rockTexture, TexCoord * u_textureScale).rgb;
    vec3 snowColor = texture(u_snowTexture, TexCoord * u_textureScale).rgb;
    
    // Combinar texturas
    vec3 baseColor = grassColor * grassWeight +
                     dirtColor * dirtWeight +
                     rockColor * rockWeight +
                     snowColor * snowWeight;
    
    // ============================================
    // 3. DETAIL MAP 
    // ============================================
    float detail = texture(u_detailMap, TexCoord * u_detailScale).r;
    
    // Detail varia de 0.5 a 1.5 (0.5 = mais escuro, 1.5 = mais claro)
    // Valor 1.0 = neutro (não altera)
    float detailFactor = mix(1.0, detail * 2.0, u_detailStrength);
    
    baseColor *= detailFactor;
    
    // ============================================
    // 4. LIGHTING (Blinn-Phong)
    // ============================================
    vec3 lightDir = normalize(u_lightPos - FragPos);
    vec3 viewDir = normalize(u_viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    
    // Ambient
    vec3 ambient = 0.3 * u_lightColor;
    
    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * u_lightColor;
    
    // Specular (mais brilho em neve/pedra)
    float specularStrength = snowWeight * 0.5 + rockWeight * 0.3;
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 64.0);
    vec3 specular = specularStrength * spec * u_lightColor;
    
    // Resultado final
    vec3 lighting = ambient + diffuse + specular;
    vec3 finalColor = baseColor * lighting;
    
    // ============================================
    // 5. FOG  
    // ============================================
    float distance = length(u_viewPos - FragPos);
    float fogAmount = 1.0 - exp(-distance * 0.005);
    fogAmount = clamp(fogAmount, 0.0, 0.5);
    vec3 fogColor = vec3(0.5, 0.6, 0.7);
    finalColor = mix(finalColor, fogColor, fogAmount);


    bool isUnderwater = u_viewPos.y < u_waterLevel;
    
    if (isUnderwater)
    {
         

    vec2 causticUV = FragPos.xz * 0.1 + u_time * 0.02;
    
    float caustic1 = texture(u_detailMap, causticUV).r;
    float caustic2 = texture(u_detailMap, causticUV * 1.3 + vec2(0.5)).r;
    float caustics = (caustic1 + caustic2) * 0.5;
    
    // Intensity varia com profundidade (menos na superfície)
    float causticIntensity = smoothstep(0.0, 5.0, u_waterLevel - FragPos.y);
    causticIntensity *= 0.3;  // Ajusta intensidade
    
    // Aplica caustics antes do fog
    finalColor += vec3(caustics) * causticIntensity;

        float underwaterDepth = distance;
        float underwaterFog = 1.0 - exp(-underwaterDepth * u_underwaterFogDensity);
        underwaterFog = clamp(underwaterFog, 0.0, 0.95);
  
        finalColor = mix(finalColor, u_underwaterFogColor, underwaterFog);
    }
    else
    {
     
        finalColor = mix(finalColor, fogColor, fogAmount);
    }
    
    
    FragColor = vec4(finalColor, 1.0);
}
