#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in float aProvince;

uniform mat4 uModel;
uniform mat4 uViewProj;

flat out vec3 vColor;     // pas d'interpolation -> facettes politiques nettes
flat out float vProvince;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vColor = aColor;
    vProvince = aProvince;
    gl_Position = uViewProj * worldPos;
}
