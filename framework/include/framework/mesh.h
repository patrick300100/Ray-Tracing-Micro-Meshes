#pragma once
#include "image.h"
// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
#include <glm/gtc/quaternion.hpp>
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
	std::string name;
	glm::mat4 offsetMatrix; // Transforms from mesh space to bone space
};

struct AnimationKeyframe {
	float time;
	glm::vec3 translation;
	glm::quat rotation;
	glm::vec3 scale;
};

struct Animation {
	std::string name;
	std::vector<std::vector<AnimationKeyframe>> boneKeyframes;
	float duration;
};

struct Mesh {
	// Vertices contain the vertex positions and normals of the mesh.
	std::vector<Vertex> vertices;
	// A triangle contains a triplet of values corresponding to the indices of the 3 vertices in the vertices array.
	std::vector<glm::uvec3> triangles;

	Material material;

	std::vector<Bone> bones;
	std::vector<Animation> animations;
};

[[nodiscard]] std::vector<Mesh> loadMesh(const std::filesystem::path& file, bool normalize = false);
[[nodiscard]] Mesh mergeMeshes(std::span<const Mesh> meshes);
void meshFlipX(Mesh& mesh);
void meshFlipY(Mesh& mesh);
void meshFlipZ(Mesh& mesh);