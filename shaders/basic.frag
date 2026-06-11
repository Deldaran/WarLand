#version 450 core

in vec3 vColor;
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uCameraPos;

out vec4 FragColor;

void main() {
    // Eclairage diffus simple depuis une lumiere directionnelle (style "soleil").
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.6));

    float diffuse = max(dot(normal, lightDir), 0.0);
    float ambient = 0.25;
    float light = ambient + 0.75 * diffuse;

    FragColor = vec4(vColor * light, 1.0);
}
