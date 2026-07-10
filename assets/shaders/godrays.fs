/*
#version 300 es
precision highp float;
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D u_sceneTexture;
uniform vec2 u_lightScreenPos;
uniform int u_numSamples;
uniform float u_exposure;
uniform float u_decay;
uniform float u_density;
uniform float u_weight;

void main()
{
    vec2 texCoord = TexCoords;
    
    // Direção do pixel atual para a posição da luz
    vec2 deltaTexCoord = (texCoord - u_lightScreenPos);
    deltaTexCoord *= u_density / float(u_numSamples);
    
    float illuminationDecay = 1.0;
    vec3 rayColor = vec3(0.0);
    
    // Sample ao longo do raio
    for(int i = 0; i < 150; i++) {
        if(i >= u_numSamples) break;
        
        texCoord -= deltaTexCoord;  // Move em direção à luz
        
        // Sample da textura de oclusão
        vec3 sampleColor = texture(u_sceneTexture, texCoord).rgb;
        
        // Acumula TODOS os samples (não filtrar!)
        sampleColor *= illuminationDecay * u_weight;
        rayColor += sampleColor;
        
        // Decay da iluminação ao longo do raio
        illuminationDecay *= u_decay;
    }
    
    // Aplica exposure final
    FragColor = vec4(rayColor * u_exposure, 1.0);
}
*/

#version 300 es
precision highp float;
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D u_sceneTexture;
uniform vec2 u_lightScreenPos;
uniform int u_numSamples;
uniform float u_exposure;
uniform float u_decay;
uniform float u_density;
uniform float u_weight;

void main()
{
    vec2 texCoord = TexCoords;
    
    // Distância do pixel à luz (para falloff)
    float distToLight = length(texCoord - u_lightScreenPos);
    
    // Early exit se muito longe (optimização)
    if(distToLight > 1.5) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    
    // Direção do raio
    vec2 deltaTexCoord = (texCoord - u_lightScreenPos);
    deltaTexCoord *= u_density / float(u_numSamples);
    
    float illuminationDecay = 1.0;
    vec3 rayColor = vec3(0.0);
    
    // Radial blur sampling
    for(int i = 0; i < 150; i++) {
        if(i >= u_numSamples) break;
        
        texCoord -= deltaTexCoord;
        
        // Clamp para evitar samples fora do ecrã
        if(texCoord.x < 0.0 || texCoord.x > 1.0 || 
           texCoord.y < 0.0 || texCoord.y > 1.0) {
            break;
        }
        
        vec3 sampleColor = texture(u_sceneTexture, texCoord).rgb;
        
        // Acumula com decay
        sampleColor *= illuminationDecay * u_weight;
        rayColor += sampleColor;
        
        illuminationDecay *= u_decay;
    }
    
    // Falloff radial (opcional, para efeito mais suave)
    float radialFalloff = 1.0 - smoothstep(0.0, 1.2, distToLight);
    rayColor *= radialFalloff;
    
    FragColor = vec4(rayColor * u_exposure, 1.0);
}
