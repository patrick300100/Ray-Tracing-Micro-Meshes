#version 450

layout(std140, binding = 0) uniform BoneMatrices {
    mat4 boneTransforms[50];
};

layout(location = 0) uniform mat4 mvpMatrix;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in ivec4 boneIndices;
layout(location = 4) in vec4 boneWeights;

out vec3 fragNormal;

void main() {
    mat4 skinMatrix =
        boneWeights.x * boneTransforms[boneIndices.x] +
        boneWeights.y * boneTransforms[boneIndices.y] +
        boneWeights.z * boneTransforms[boneIndices.z] +
        boneWeights.w * boneTransforms[boneIndices.w];

    fragNormal = normal;
    gl_Position = mvpMatrix * skinMatrix * vec4(position, 1.0);
}