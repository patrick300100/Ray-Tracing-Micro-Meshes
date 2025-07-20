#pragma once

#include <framework/disable_all_warnings.h>
#include <glm/gtc/quaternion.hpp>
#include "TransformationChannel.h"
#include <vector>
#include "../../src/Triangle2D.h"
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()

struct uVertex {
	glm::vec3 position;
	glm::vec3 displacement;
};

struct Triangle {
	glm::uvec3 baseVertexIndices; //Indices of the vertices array of the Mesh struct
	std::vector<uVertex> uVertices; //Since base vertices can also be part of a micro triangle, this vector also contains base vertices
	std::vector<glm::uvec3> uFaces; //Indices to the uVertices vector that determine which micro vertices make up for a micro triangle.

	[[nodiscard]] std::vector<glm::vec3> baryCoords(const glm::vec3& A, const glm::vec3& B, const glm::vec3& C) const;
	static glm::vec3 computeBaryCoords(const glm::vec3& A, const glm::vec3& B, const glm::vec3& C, const glm::vec3& pos);
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::ivec4 boneIndices = glm::ivec4(0);
	glm::vec4 boneWeights = glm::vec4(0.0f);
	glm::vec3 direction;

	//Only applicable for micro vertices! These refer to the 3 bone indices and weights of the base vertices under which this micro vertex falls.
	glm::ivec4 baseBoneIndices0{0};
	glm::ivec4 baseBoneIndices1{0};
	glm::ivec4 baseBoneIndices2{0};
	glm::vec4 baseBoneWeights0{0};
	glm::vec4 baseBoneWeights1{0};
	glm::vec4 baseBoneWeights2{0};
	glm::vec3 baryCoords;

	[[nodiscard]] bool operator==(const Vertex&) const noexcept = default;
};

struct Bone {
	TransformationChannel<glm::vec3> translation;
	TransformationChannel<glm::quat> rotation;
	TransformationChannel<glm::vec3> scale;
	glm::mat4 ibm{}; //Inverse bind matrix. Used to go from mesh space to bone space

	glm::mat4 transformationMatrix(float animTime);
};

class Mesh {
public:
	std::vector<Vertex> vertices;
	std::vector<Triangle> triangles;

	std::vector<Bone> bones;
	std::vector<int> parent;

	std::vector<glm::mat4> boneTransformations(float animTime);
	glm::mat4 globalTransform(float animTime, int index);
	[[nodiscard]] float animationDuration() const;
	[[nodiscard]] std::vector<glm::uvec3> baseTriangleIndices() const;
	[[nodiscard]] std::pair<std::vector<Vertex>, std::vector<glm::uvec3>> allTriangles() const; //Contains base vertices + micro vertices

	[[nodiscard]] int numberOfVerticesOnEdge() const; //Computes the number of (micro) vertices on an edge
	[[nodiscard]] int subdivisionLevel() const; //Computes the subdivision level of the triangles (same for each triangle)

	[[nodiscard]] std::vector<glm::vec2> minMaxDisplacements(std::vector<int>& offsets) const; //Compute hierarchical minimum and maximum displacements

	//Compute delta for each hierarchical triangle in 2D. Delta represents a scalar by how much to expand the edges to include micro-vertices of future subdivision levels.
	//So if we have a displaced triangle, and we project it onto the base triangle's plane, we compute 3 vertex positions that bounds all micro-vertices in that triangle (also that of
	//future subdivision levels).
	//We return a vector that contains deltas hierarchically, but do not store the lowest subdivision level. So if a triangle has subdivision level 2, a total of 5 deltas will be
	//made for a single triangle. One delta for level 0, and four deltas for level 1.
	[[nodiscard]] std::vector<float> triangleDeltas(const std::vector<int>& dOffsets) const;
};
