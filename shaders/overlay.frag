#version 450 core

flat in int vProvince;
in vec3 vWorldPos;

uniform vec3 uSunDir;
uniform float uAlpha;
uniform float uSelected;          // id de la province selectionnee (-1 = aucune)
uniform sampler2D uProvColor;     // couleur par province (texture provinceCount x 1)

out vec4 FragColor;

void main() {
    vec3 base = texelFetch(uProvColor, ivec2(vProvince, 0), 0).rgb;

    vec3 normal = normalize(vWorldPos);
    float diffuse = max(dot(normal, uSunDir), 0.0);
    vec3 color = base * (0.40 + 0.70 * diffuse);
    float alpha = uAlpha;

    if (vProvince == int(uSelected + 0.5)) {
        color = mix(color, vec3(1.0), 0.45);
        alpha = min(1.0, uAlpha + 0.30);
    }

    FragColor = vec4(color, alpha);
}
