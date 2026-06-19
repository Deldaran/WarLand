#version 450 core

in vec3 vDir;
out vec4 FragColor;

uniform samplerCube uSky;

void main() {
    FragColor = vec4(texture(uSky, normalize(vDir)).rgb, 1.0);
}
