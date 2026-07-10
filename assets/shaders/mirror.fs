#version 300 es
precision highp float;

out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

uniform sampler2D u_reflectionTexture;
uniform vec3 u_viewPos;
uniform float u_fresnelPower;
uniform float u_fresnelMin;
uniform vec3 u_mirrorTint;
 
 

void main()
{           
    vec3 N = normalize(Normal);
    vec3 V = normalize(u_viewPos - FragPos);
    
    // Water distortion (optional)
    vec2 distortion = vec2(0.0);
  
    
    vec2 finalUV = TexCoords + distortion;
    
    // Fresnel effect
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, u_fresnelPower);
    fresnel = mix(u_fresnelMin, 1.0, fresnel);
    
    // Sample reflection
    vec3 reflection = texture(u_reflectionTexture, finalUV).rgb;
    
    // Base color
    vec3 baseColor = u_mirrorTint * 0.1;
  
    
    // Mix
    vec3 color = mix(baseColor, reflection, fresnel);
    
    // Specular highlight
    vec3 specular = vec3(pow(NdotV, 50.0)) * 0.2;
    color += specular;
    
    float alpha =  1.0;
    FragColor = vec4(color, alpha);
}