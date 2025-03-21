#pragma once
#include "image.h"
// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
#include <glm/gtc/quaternion.hpp>
#include <tinygltf/tiny_gltf.h>

#include "micro.h"
#include "TransformationChannel.h"
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

struct uVertex {
	glm::vec3 position;
	glm::vec3 displacement;
};

struct Triangle {
	glm::uvec3 baseVertexIndices; //Indices of the vertices array of the Mesh struct
	std::vector<uVertex> uVertices; //Since base vertices can also be part of a micro triangle, this vector also contains base vertices
	std::vector<glm::uvec3> uFaces; //Indices to the uVertices vector that determine which micro vertices make up for a micro triangle.

	[[nodiscard]] std::vector<glm::vec3> baryCoords(const glm::vec3& A, const glm::vec3& B, const glm::vec3& C) const {
		std::vector<glm::vec3> baryCoords;
		baryCoords.reserve(uVertices.size());

		for(const auto& uv : uVertices) {
			baryCoords.push_back(computeBaryCoords(A, B, C, uv.position));
		}

		return baryCoords;
	}

	[[nodiscard]] static glm::vec3 computeBaryCoords(const glm::vec3& A, const glm::vec3& B, const glm::vec3& C, const glm::vec3& pos) {
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
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;

	glm::ivec4 boneIndices = glm::ivec4(0);
	glm::vec4 boneWeights = glm::vec4(0.0f);

	glm::vec3 displacement;

	[[nodiscard]] constexpr bool operator==(const Vertex&) const noexcept = default;
};

struct Bone {
	TransformationChannel<glm::vec3> translation;
	TransformationChannel<glm::quat> rotation;
	TransformationChannel<glm::vec3> scale;
	glm::mat4 ibm{};

	[[nodiscard]] glm::mat4 transformationMatrix(const float animTime) {
		const auto t1 = translation.getTransformation(animTime);
		const auto t2 = rotation.getTransformation(animTime);
		const auto t3 = scale.getTransformation(animTime);

		return glm::translate(glm::mat4(1.0f), t1) * glm::mat4_cast(t2) * glm::scale(glm::mat4(1.0f), t3);
	}
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<Triangle> triangles;

	std::vector<Bone> bones;

	std::vector<int> parent;

	[[nodiscard]] std::vector<glm::mat4> boneTransformations(const float animTime) {
		std::vector<glm::mat4> transformations = {};

		for(int i = 0; i < bones.size(); i++) {
			auto globalT = globalTransform(animTime, i);

			transformations.push_back(globalT * bones[i].ibm);
		}

		return transformations;
	}

	glm::mat4 globalTransform(const float animTime, const int index) {
		if(parent[index] == -1) return bones[index].transformationMatrix(animTime);

		return globalTransform(animTime, parent[index]) * bones[index].transformationMatrix(animTime);
	}

	float animationDuration() {
		return bones[0].translation.animationDuration();
	}

	std::vector<glm::uvec3> baseTriangleIndices;
};