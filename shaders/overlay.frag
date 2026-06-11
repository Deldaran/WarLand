#version 450 core

flat in vec3 vColor;
in vec3 vWorldPos;

uniform vec3 uSunDir;
uniform float uAlpha;

out vec4 FragColor;

void main() {
    // Normale approchee = direction depuis le centre (sphere centree a l'origine).
    vec3 normal = normalize(vWorldPos);
    float diffuse = max(dot(normal, uSunDir), 0.0);
    vec3 color = vColor * (0.40 + 0.70 * diffuse);
    FragColor = vec4(color, uAlpha);
}
