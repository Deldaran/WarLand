#version 450 core

in vec2 vUV;
flat in float vLayer;
out vec4 FragColor;

uniform sampler2DArray uAtlas;
uniform vec3 uSunDir;

void main() {
    vec4 c = texture(uAtlas, vec3(vUV, vLayer));
    if (c.a < 0.4) discard;       // alpha-test : bords nets (sprites Daggerfall)
    float light = 0.78 + 0.22 * max(uSunDir.y, 0.0); // petit eclairage diffus
    FragColor = vec4(c.rgb * light, 1.0);
}
