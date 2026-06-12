#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aSize;
layout(location = 2) in vec3 aColor;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vColor;

void main() {
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
    gl_PointSize = aSize;
    vColor = aColor;
}
