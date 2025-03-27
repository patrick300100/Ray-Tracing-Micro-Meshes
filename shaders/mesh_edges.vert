#version 450

layout(std140, binding = 0) uniform BoneMatrices {
    mat4 boneTransforms[50];
};

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
    mat4 bv0SkinMatrix = baseBoneWeights0.x * boneTransforms[baseBoneIndices0.x] +
                         baseBoneWeights0.y * boneTransforms[baseBoneIndices0.y] +
                         baseBoneWeights0.z * boneTransforms[baseBoneIndices0.z] +
                         baseBoneWeights0.w * boneTransforms[baseBoneIndices0.w];

    mat4 bv1SkinMatrix = baseBoneWeights1.x * boneTransforms[baseBoneIndices1.x] +
                         baseBoneWeights1.y * boneTransforms[baseBoneIndices1.y] +
                         baseBoneWeights1.z * boneTransforms[baseBoneIndices1.z] +
                         baseBoneWeights1.w * boneTransforms[baseBoneIndices1.w];

    mat4 bv2SkinMatrix = baseBoneWeights2.x * boneTransforms[baseBoneIndices2.x] +
                         baseBoneWeights2.y * boneTransforms[baseBoneIndices2.y] +
                         baseBoneWeights2.z * boneTransforms[baseBoneIndices2.z] +
                         baseBoneWeights2.w * boneTransforms[baseBoneIndices2.w];

    mat4 skinMatrix = baryCoords.x * bv0SkinMatrix + baryCoords.y * bv1SkinMatrix + baryCoords.z * bv2SkinMatrix;

    vec3 skinnedPos = (vec4(position + displacementScale * displacement, 1.0f) * skinMatrix).xyz;
    skinnedPos += 0.001f * displacement; //Add tiny displacement to avoid z-fighting with mesh

    gl_Position = mvp * vec4(skinnedPos, 1.0f);
}