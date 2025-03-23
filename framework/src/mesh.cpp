#include "mesh.h"

#include <ranges>
#include <unordered_map>

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

std::vector<glm::vec3> Triangle::baryCoords(const glm::vec3& A, const glm::vec3 &B, const glm::vec3& C) const {
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

template <class T>
static void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct VertexHash {
    size_t operator()(const Vertex& v) const {
        size_t seed = 0;
        hash_combine(seed, v.position.x);
        hash_combine(seed, v.position.y);
        hash_combine(seed, v.position.z);

        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneIndices0[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneIndices1[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneIndices2[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneWeights0[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneWeights1[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneWeights2[j]);

        hash_combine(seed, v.baryCoords.x);
        hash_combine(seed, v.baryCoords.y);
        hash_combine(seed, v.baryCoords.z);

        return seed;
    }
};

std::pair<std::vector<Vertex>, std::vector<glm::uvec3>> Mesh::allTriangles() const {
    std::unordered_map<Vertex, uint32_t, VertexHash> vertexCache;

    std::vector<Vertex> vs(vertices.size()); //Reserved size is lower bound (needs more space than reserved).
    std::vector<glm::uvec3> is(triangles.size()); //Reserved size is lower bound (needs more space than reserved).

    for(const auto& t : triangles) {
        //Base vertices
        const auto& bv0 = vertices[t.baseVertexIndices[0]];
        const auto& bv1 = vertices[t.baseVertexIndices[1]];
        const auto& bv2 = vertices[t.baseVertexIndices[2]];

        for(const auto& f : t.uFaces) {
            glm::uvec3 triangle;

            for(int i = 0; i < 3; i++) {
                const auto& uv = t.uVertices[f[i]];

                const auto bc = t.computeBaryCoords(bv0.position, bv1.position, bv2.position, uv.position);

                Vertex v {
                    .position = uv.position,
                    .normal = bc.x * bv0.normal + bc.y * bv1.normal + bc.z * bv2.normal, //interpolated normal
                    .displacement = uv.displacement,
                    .baseBoneIndices0 = bv0.boneIndices,
                    .baseBoneIndices1 = bv1.boneIndices,
                    .baseBoneIndices2 = bv2.boneIndices,
                    .baseBoneWeights0 = bv0.boneWeights,
                    .baseBoneWeights1 = bv1.boneWeights,
                    .baseBoneWeights2 = bv2.boneWeights,
                    .baryCoords = bc
                };

                if(auto iter = vertexCache.find(v); iter != vertexCache.end()) {
                    // Already visited this vertex? Reuse it!
                    triangle[i] = iter->second;
                } else {
                    // New vertex? Create it and store it in the vertex cache.
                    vertexCache[v] = triangle[i] = vs.size();
                    vs.push_back(v);
                }
            }

            is.push_back(triangle);
        }
    }

    return {vs, is};
}
