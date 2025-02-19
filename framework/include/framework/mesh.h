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
	std::string name;
	glm::mat4 offsetMatrix; // Transforms from mesh space to bone space
};

struct Animation {
	TransformationChannel<glm::vec3> translation;
	TransformationChannel<glm::quat> rotation;
	TransformationChannel<glm::vec3> scale;
	double duration = 0.0;

	glm::mat4 transformationMatrix(float currentTime) {
		auto t1 = translation.getTransformation(currentTime);
		auto t2 = rotation.getTransformation(currentTime);
		auto t3 = scale.getTransformation(currentTime);

		return glm::translate(glm::mat4(1.0f), t1) * glm::mat4_cast(t2) * glm::scale(glm::mat4(1.0f), t3);
	}
};

struct Mesh {
	// Vertices contain the vertex positions and normals of the mesh.
	std::vector<Vertex> vertices;
	// A triangle contains a triplet of values corresponding to the indices of the 3 vertices in the vertices array.
	std::vector<glm::uvec3> triangles;

	Material material;

	std::vector<Bone> bones;
	std::vector<Animation> animation; //Animation for each bone
};

[[nodiscard]] std::vector<Mesh> loadMesh(const std::filesystem::path& file, bool normalize = false);
std::vector<Mesh> loadMeshGLTF(const std::filesystem::path& file);
[[nodiscard]] Mesh mergeMeshes(std::span<const Mesh> meshes);
void meshFlipX(Mesh& mesh);
void meshFlipY(Mesh& mesh);
void meshFlipZ(Mesh& mesh);