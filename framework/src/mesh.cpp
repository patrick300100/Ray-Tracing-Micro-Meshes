#include "mesh.h"

#include <ranges>

std::vector<glm::mat4> Mesh::boneTransformations(const float animTime) {
    std::vector<glm::mat4> transformations = {};

    for(int i = 0; i < bones.size(); i++) {
        auto globalT = globalTransform(animTime, i);

        transformations.push_back(globalT * bones[i].ibm);
    }

    return transformations;
}

glm::mat4 Mesh::globalTransform(const float animTime, const int index) {
    if(parent[index] == -1) return bones[index].transformationMatrix(animTime);

    return globalTransform(animTime, parent[index]) * bones[index].transformationMatrix(animTime);
}

float Mesh::animationDuration() const {
    return bones[0].translation.animationDuration();
}

std::vector<glm::uvec3> Mesh::baseTriangleIndices() const {
    const auto mapped = triangles | std::ranges::views::transform([](const Triangle& t) { return t.baseVertexIndices; });

    return {mapped.begin(), mapped.end()};
}

std::vector<glm::vec3> Triangle::baryCoords(const glm::vec3 &A, const glm::vec3 &B, const glm::vec3 &C) const {
    std::vector<glm::vec3> baryCoords;
    baryCoords.reserve(uVertices.size());

    for(const auto& uv : uVertices) {
        baryCoords.push_back(computeBaryCoords(A, B, C, uv.position));
    }

    return baryCoords;
}

glm::vec3 Triangle::computeBaryCoords(const glm::vec3 &A, const glm::vec3 &B, const glm::vec3 &C, const glm::vec3 &pos) {
    const glm::vec3 v0 = B - A, v1 = C - A, v2 = pos - A;

    const float d00 = glm::dot(v0, v0);
    const float d01 = glm::dot(v0, v1);
    const float d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0);
    const float d21 = glm::dot(v2, v1);

    const float denom = d00 * d11 - d01 * d01;
    const float beta = (d11 * d20 - d01 * d21) / denom;
    const float gamma = (d00 * d21 - d01 * d20) / denom;
    const float alpha = 1.0f - beta - gamma;

    return {alpha, beta, gamma};
}

glm::mat4 Bone::transformationMatrix(const float animTime) {
    const auto t1 = translation.getTransformation(animTime);
    const auto t2 = rotation.getTransformation(animTime);
    const auto t3 = scale.getTransformation(animTime);

    return glm::translate(glm::mat4(1.0f), t1) * glm::mat4_cast(t2) * glm::scale(glm::mat4(1.0f), t3);
}
