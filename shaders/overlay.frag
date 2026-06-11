#version 450 core

flat in vec3 vColor;
flat in float vProvince;
in vec3 vWorldPos;

uniform vec3 uSunDir;
uniform float uAlpha;
uniform float uSelected;  // id de la province selectionnee (-1 = aucune)

out vec4 FragColor;

void main() {
    // Normale approchee = direction depuis le centre (sphere centree a l'origine).
    vec3 normal = normalize(vWorldPos);
    float diffuse = max(dot(normal, uSunDir), 0.0);
    vec3 color = vColor * (0.40 + 0.70 * diffuse);
    float alpha = uAlpha;

    // Surlignage de la province selectionnee.
    if (abs(vProvince - uSelected) < 0.5) {
        color = mix(color, vec3(1.0), 0.45);
        alpha = min(1.0, uAlpha + 0.30);
    }

    FragColor = vec4(color, alpha);
}
