#version 450

layout(location = 0) uniform mat4 mvp;
layout(location = 1) uniform float displacementScale;

layout(location = 0) in vec3 position;
layout(location = 1) in ivec4 boneIndices;
layout(location = 2) in vec4 boneWeights;
layout(location = 3) in vec3 displacement;
layout(location = 4) in ivec4 baseBoneIndices0;
layout(location = 5) in ivec4 baseBoneIndices1;
layout(location = 6) in ivec4 baseBoneIndices2;
layout(location = 7) in vec4 baseBoneWeights0;
layout(location = 8) in vec4 baseBoneWeights1;
layout(location = 9) in vec4 baseBoneWeights2;
layout(location = 10) in vec3 baryCoords;

void main(void) {
    vec4 pos = vec4(position + displacementScale * displacement, 1.0f);
    gl_Position = mvp * pos;
}