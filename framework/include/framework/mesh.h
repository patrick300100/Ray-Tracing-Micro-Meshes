#pragma once
#include "image.h"
// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
#include <glm/gtc/quaternion.hpp>
#include <tinygltf/tiny_gltf.h>

#include "TransformationChannel.h"
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texCoord; // Texture coordinate

	glm::ivec4 boneIndices = glm::ivec4(0);
	glm::vec4 boneWeights = glm::vec4(0.0f);

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
	std::vector<glm::uvec3> triangles; //Contains a triplet of values corresponding to the indices of the 3 vertices in the vertices array.

	Material material;

	std::vector<Bone> bones;

	std::vector<int> parent;

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
};

[[nodiscard]] std::vector<Mesh> loadMesh(const std::filesystem::path& file, bool normalize = false);
[[nodiscard]] Mesh mergeMeshes(std::span<const Mesh> meshes);
void meshFlipX(Mesh& mesh);
void meshFlipY(Mesh& mesh);
void meshFlipZ(Mesh& mesh);