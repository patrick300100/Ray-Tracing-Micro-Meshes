#version 450

layout(location = 0) uniform mat4 mvp;
layout(location = 1) uniform float displacementScale;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 displacement;

void main(void) {
    vec4 pos = vec4(position + displacementScale * displacement, 1.0f);
    gl_Position = mvp * pos;
}