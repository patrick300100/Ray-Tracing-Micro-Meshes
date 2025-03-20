#version 450

layout(location = 2) uniform vec3 edgeColor;

layout(location = 0) out vec4 color;

void main(void) {
    color = vec4(edgeColor, 1.0f);
}