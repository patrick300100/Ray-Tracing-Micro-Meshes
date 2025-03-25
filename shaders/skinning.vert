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
layout(location = 5) in ivec4 baseBoneIndices0;
layout(location = 6) in ivec4 baseBoneIndices1;
layout(location = 7) in ivec4 baseBoneIndices2;
layout(location = 8) in vec4 baseBoneWeights0;
layout(location = 9) in vec4 baseBoneWeights1;
layout(location = 10) in vec4 baseBoneWeights2;
layout(location = 11) in vec3 baryCoords;

out vec3 fragNormal;
out vec3 fragSurfacePos;

void main() {
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

    //For some reason we need to take the transpose
    bv0SkinMatrix = transpose(bv0SkinMatrix);
    bv1SkinMatrix = transpose(bv1SkinMatrix);
    bv2SkinMatrix = transpose(bv2SkinMatrix);

    mat4 interpolatedSkinMatrix = baryCoords.x * bv0SkinMatrix + baryCoords.y * bv1SkinMatrix + baryCoords.z * bv2SkinMatrix;

    vec4 newPos = vec4(position + displacementScale * displacement, 1.0f) * interpolatedSkinMatrix;

    gl_Position = mvpMatrix * newPos;
    fragSurfacePos = (mvMatrix * newPos).xyz;
    fragNormal = normal;
}