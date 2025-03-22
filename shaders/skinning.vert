#version 450

layout(std140, binding = 0) uniform BoneMatrices {
    mat4 boneTransforms[50];
};

layout(location = 0) uniform mat4 mvpMatrix;
layout(location = 1) uniform mat4 mvMatrix;
layout(location = 2) uniform float displacementScale;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in ivec4 boneIndices;
layout(location = 3) in vec4 boneWeights;
layout(location = 4) in vec3 displacement;

out vec3 fragNormal;
out vec3 fragSurfacePos;

void main() {
    vec4 pos = vec4(position + displacementScale * displacement, 1.0);

    mat4 skinMatrix =
        boneWeights.x * boneTransforms[boneIndices.x] +
        boneWeights.y * boneTransforms[boneIndices.y] +
        boneWeights.z * boneTransforms[boneIndices.z] +
        boneWeights.w * boneTransforms[boneIndices.w];

    gl_Position = mvpMatrix * skinMatrix * pos;

    fragSurfacePos = (mvMatrix * pos).xyz;
    fragNormal = normal;
}