#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aProvince;

uniform mat4 uModel;
uniform mat4 uViewProj;

flat out int vProvince;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vProvince = int(aProvince + 0.5);
    gl_Position = uViewProj * worldPos;
}
