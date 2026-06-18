#version 450 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uAtlas;
uniform vec3 uSunDir;

void main() {
    vec4 c = texture(uAtlas, vUV);
    if (c.a < 0.4) discard;       // alpha-test : bords nets (sprites Daggerfall)
    // Petit eclairage diffus pour ancrer les props dans la scene.
    float light = 0.75 + 0.25 * max(uSunDir.y, 0.0);
    FragColor = vec4(c.rgb * light, 1.0);
}
