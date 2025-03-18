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
			glm::vec3 v0 = B - A, v1 = C - A, v2 = uv.position - A;

			const float d00 = glm::dot(v0, v0);
			const float d01 = glm::dot(v0, v1);
			const float d11 = glm::dot(v1, v1);
			const float d20 = glm::dot(v2, v0);
			const float d21 = glm::dot(v2, v1);

			const float denom = d00 * d11 - d01 * d01;
			const float beta = (d11 * d20 - d01 * d21) / denom;
			const float gamma = (d00 * d21 - d01 * d20) / denom;
			const float alpha = 1.0f - beta - gamma;

			baryCoords.emplace_back(alpha, beta, gamma);
		}

		return baryCoords;
	}
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texCoord; // Texture coordinate

	glm::ivec4 boneIndices = glm::ivec4(0);
	glm::vec4 boneWeights = glm::vec4(0.0f);

	glm::vec3 displacement;

	[[nodiscard]] constexpr bool operator==(const Vertex&) const noexcept = default;
};

struct Material {
	glm::vec3 kd; // Diffuse color
	glm::vec3 ks{ 0.0f };
	float shininess{ 1.0f };
	float transparency{ 1.0f };

	// Optional texture that replaces kd; use as follows:
	// 
	// if (material.kdTexture) {
	//   material.kdTexture->getTexel(...);
	// }
	std::shared_ptr<Image> kdTexture;
};

struct Bone {
	TransformationChannel<glm::vec3> translation;
	TransformationChannel<glm::quat> rotation;
	TransformationChannel<glm::vec3> scale;
	glm::mat4 ibm{};

	[[nodiscard]] glm::mat4 transformationMatrix(const float currentTime) {
		const auto t1 = translation.getTransformation(currentTime);
		const auto t2 = rotation.getTransformation(currentTime);
		const auto t3 = scale.getTransformation(currentTime);

		return glm::translate(glm::mat4(1.0f), t1) * glm::mat4_cast(t2) * glm::scale(glm::mat4(1.0f), t3);
	}
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<Triangle> triangles;

	Material material;

	std::vector<Bone> bones;

	std::vector<int> parent;

	SubdivisionMesh umesh;

	[[nodiscard]] std::vector<glm::mat4> boneTransformations(const float currentTime) {
		std::vector<glm::mat4> transformations = {};

		for(int i = 0; i < bones.size(); i++) {
			auto globalT = globalTransform(currentTime, i);

			transformations.push_back(globalT * bones[i].ibm);
		}

		return transformations;
	}

	glm::mat4 globalTransform(const float currentTime, const int index) {
		if(parent[index] == -1) return bones[index].transformationMatrix(currentTime);

		return globalTransform(currentTime, parent[index]) * bones[index].transformationMatrix(currentTime);
	}

	std::vector<glm::uvec3> baseTriangleIndices;
};