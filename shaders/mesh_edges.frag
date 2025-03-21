#version 450

layout(location = 2) uniform vec4 edgeColor;

layout(location = 0) out vec4 color;

void main(void) {
    color = edgeColor;
}