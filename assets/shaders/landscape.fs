#version 300 es
precision highp float;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
 
 

out vec4 FragColor;
 
uniform sampler2D basicTexture;      // Baixo
uniform sampler2D detailTexture;       // Médio baixo

uniform float textureScale;
uniform float detailScale;
uniform float detailStrength;

uniform vec4 clipPlane;
uniform bool useClipPlane;


void main()
{
 
    if(useClipPlane)
    {
     if (dot(vec4(FragPos, 1.0), clipPlane) < 0.0)
     {
       discard;
     }
    }
   
    vec3 normal = normalize(Normal);
    
    
    vec3 baseColor = texture(basicTexture, TexCoord * textureScale).rgb;
    
    float detail = texture(detailTexture, TexCoord *  detailScale).r;
    
    // Detail varia de 0.5 a 1.5 (0.5 = mais escuro, 1.5 = mais claro)
    // Valor 1.0 = neutro (não altera)
    float detailFactor = mix(1.0, detail * 2.0, detailStrength);
    
    baseColor *= detailFactor;
    

    
 

 
    
    
    FragColor = vec4(baseColor, 1.0);
}
