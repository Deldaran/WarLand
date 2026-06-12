#version 450 core

in vec3 vColor;
out vec4 FragColor;

void main() {
    // Point rond avec un petit halo et un centre clair (marqueur de ville).
    vec2 d = gl_PointCoord - vec2(0.5);
    float r = length(d);
    if (r > 0.5) discard;
    float core = smoothstep(0.5, 0.15, r);
    vec3 col = mix(vColor, vec3(1.0), 0.5 * core);
    FragColor = vec4(col, core);
}
