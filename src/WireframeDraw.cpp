#include "WireframeDraw.h"

#include <iostream>
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
DISABLE_WARNINGS_POP()
#include <ranges>

WireframeDraw::WireframeDraw(const Mesh& m): hashSet(m.vertices.size()), mesh{m.vertices, m.triangles} {
	auto addBaseEdge = [&](const Vertex& vA, const Vertex& vB) {
		edgeData.baseVertices.emplace_back(vA);
		edgeData.baseVertices.emplace_back(vB);

		hashSet.insert(hash(vA.position, vB.position));
	};

	auto addMicroEdge = [&](const Vertex& vA, const Vertex& vB) {
		edgeData.microVertices.emplace_back(vA);
		edgeData.microVertices.emplace_back(vB);

		hashSet.insert(hash(vA.position, vB.position));
	};


	for(const auto& t : mesh.triangles) {
		const auto& bv0 = mesh.vertices[t.baseVertexIndices[0]];
		const auto& bv1 = mesh.vertices[t.baseVertexIndices[1]];
		const auto& bv2 = mesh.vertices[t.baseVertexIndices[2]];

		for(const auto& uf : t.uFaces) {
			const auto& uv0 = t.uVertices[uf[0]];
			const auto& uv1 = t.uVertices[uf[1]];
			const auto& uv2 = t.uVertices[uf[2]];

			//Loop over each edge of the (micro) triangle
			for(const auto& [uvA, uvB] : std::vector<std::pair<uVertex, uVertex>>{{uv0, uv1}, {uv1, uv2}, {uv0, uv2}}) {
				//Barycentric coordinate of position in the middle of the edge
				const auto baryMiddle = Triangle::computeBaryCoords(
					mesh.vertices[t.baseVertexIndices[0]].position,
					mesh.vertices[t.baseVertexIndices[1]].position,
					mesh.vertices[t.baseVertexIndices[2]].position,
					(uvA.position + uvB.position) / 2.0f
				);

				const bool onBaseEdge = glm::any(glm::epsilonEqual(baryMiddle, glm::vec3(0.0f), 0.0001f)); //Edge (of micro triangle) lies on edge of base triangle
				const bool drawnBefore = contains(uvA.position, uvB.position) || contains(uvB.position, uvA.position); //This edge has already been drawn before

				const auto vA = Vertex {
					.position = uvA.position,
					.displacement = uvA.displacement,
					.baseBoneIndices0 = bv0.boneIndices,
					.baseBoneIndices1 = bv1.boneIndices,
					.baseBoneIndices2 = bv2.boneIndices,
					.baseBoneWeights0 = bv0.boneWeights,
					.baseBoneWeights1 = bv1.boneWeights,
					.baseBoneWeights2 = bv2.boneWeights,
					.baryCoords = Triangle::computeBaryCoords(bv0.position, bv1.position, bv2.position, uvA.position)
				};

				const auto vB = Vertex {
					.position = uvB.position,
					.displacement = uvB.displacement,
					.baseBoneIndices0 = bv0.boneIndices,
					.baseBoneIndices1 = bv1.boneIndices,
					.baseBoneIndices2 = bv2.boneIndices,
					.baseBoneWeights0 = bv0.boneWeights,
					.baseBoneWeights1 = bv1.boneWeights,
					.baseBoneWeights2 = bv2.boneWeights,
					.baryCoords = Triangle::computeBaryCoords(bv0.position, bv1.position, bv2.position, uvB.position)
				};

				if(!drawnBefore) {
					if(onBaseEdge) addBaseEdge(vA, vB);
					else addMicroEdge(vA, vB);
				}
			}
		}
	}

	//Create buffers for base edges
	{
		glCreateBuffers(1, &baseVBO);
		glNamedBufferData(baseVBO, static_cast<GLsizeiptr>(edgeData.baseVertices.size() * sizeof(Vertex)), edgeData.baseVertices.data(), GL_STREAM_DRAW);

		glCreateVertexArrays(1, &baseVAO);

		glVertexArrayVertexBuffer(baseVAO, 0, baseVBO, 0, sizeof(Vertex));

		glEnableVertexArrayAttrib(baseVAO, 0);
		glEnableVertexArrayAttrib(baseVAO, 1);
		glEnableVertexArrayAttrib(baseVAO, 2);
		glEnableVertexArrayAttrib(baseVAO, 3);
		glEnableVertexArrayAttrib(baseVAO, 4);
		glEnableVertexArrayAttrib(baseVAO, 5);
		glEnableVertexArrayAttrib(baseVAO, 6);
		glEnableVertexArrayAttrib(baseVAO, 7);
		glEnableVertexArrayAttrib(baseVAO, 8);
		glEnableVertexArrayAttrib(baseVAO, 9);
		glEnableVertexArrayAttrib(baseVAO, 10);

		glVertexArrayAttribFormat(baseVAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
		glVertexArrayAttribIFormat(baseVAO, 1, 4, GL_INT, offsetof(Vertex, boneIndices));
		glVertexArrayAttribFormat(baseVAO, 2, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, boneWeights));
		glVertexArrayAttribFormat(baseVAO, 3, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, displacement));
		glVertexArrayAttribIFormat(baseVAO, 4, 4, GL_INT, offsetof(Vertex, baseBoneIndices0));
		glVertexArrayAttribIFormat(baseVAO, 5, 4, GL_INT, offsetof(Vertex, baseBoneIndices1));
		glVertexArrayAttribIFormat(baseVAO, 6, 4, GL_INT, offsetof(Vertex, baseBoneIndices2));
		glVertexArrayAttribFormat(baseVAO, 7, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, baseBoneWeights0));
		glVertexArrayAttribFormat(baseVAO, 8, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, baseBoneWeights1));
		glVertexArrayAttribFormat(baseVAO, 9, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, baseBoneWeights2));
		glVertexArrayAttribFormat(baseVAO, 10, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, baryCoords));

		glVertexArrayAttribBinding(baseVAO, 0, 0);
		glVertexArrayAttribBinding(baseVAO, 1, 0);
		glVertexArrayAttribBinding(baseVAO, 2, 0);
		glVertexArrayAttribBinding(baseVAO, 3, 0);
		glVertexArrayAttribBinding(baseVAO, 4, 0);
		glVertexArrayAttribBinding(baseVAO, 5, 0);
		glVertexArrayAttribBinding(baseVAO, 6, 0);
		glVertexArrayAttribBinding(baseVAO, 7, 0);
		glVertexArrayAttribBinding(baseVAO, 8, 0);
		glVertexArrayAttribBinding(baseVAO, 9, 0);
		glVertexArrayAttribBinding(baseVAO, 10, 0);
	}

	//Create buffers for micro edges
	{
		glCreateBuffers(1, &microVBO);
		glNamedBufferData(microVBO, static_cast<GLsizeiptr>(edgeData.microVertices.size() * sizeof(Vertex)), edgeData.microVertices.data(), GL_STREAM_DRAW);

		glCreateVertexArrays(1, &microVAO);

		glVertexArrayVertexBuffer(microVAO, 0, microVBO, 0, sizeof(Vertex));

		glEnableVertexArrayAttrib(microVAO, 0);
		glEnableVertexArrayAttrib(microVAO, 1);
		glEnableVertexArrayAttrib(microVAO, 2);
		glEnableVertexArrayAttrib(microVAO, 3);
		glEnableVertexArrayAttrib(microVAO, 4);
		glEnableVertexArrayAttrib(microVAO, 5);
		glEnableVertexArrayAttrib(microVAO, 6);
		glEnableVertexArrayAttrib(microVAO, 7);
		glEnableVertexArrayAttrib(microVAO, 8);
		glEnableVertexArrayAttrib(microVAO, 9);
		glEnableVertexArrayAttrib(microVAO, 10);

		glVertexArrayAttribFormat(microVAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
		glVertexArrayAttribIFormat(microVAO, 1, 4, GL_INT, offsetof(Vertex, boneIndices));
		glVertexArrayAttribFormat(microVAO, 2, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, boneWeights));
		glVertexArrayAttribFormat(microVAO, 3, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, displacement));
		glVertexArrayAttribIFormat(microVAO, 4, 4, GL_INT, offsetof(Vertex, baseBoneIndices0));
		glVertexArrayAttribIFormat(microVAO, 5, 4, GL_INT, offsetof(Vertex, baseBoneIndices1));
		glVertexArrayAttribIFormat(microVAO, 6, 4, GL_INT, offsetof(Vertex, baseBoneIndices2));
		glVertexArrayAttribFormat(microVAO, 7, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, baseBoneWeights0));
		glVertexArrayAttribFormat(microVAO, 8, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, baseBoneWeights1));
		glVertexArrayAttribFormat(microVAO, 9, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, baseBoneWeights2));
		glVertexArrayAttribFormat(microVAO, 10, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, baryCoords));

		glVertexArrayAttribBinding(microVAO, 0, 0);
		glVertexArrayAttribBinding(microVAO, 1, 0);
		glVertexArrayAttribBinding(microVAO, 2, 0);
		glVertexArrayAttribBinding(microVAO, 3, 0);
		glVertexArrayAttribBinding(microVAO, 4, 0);
		glVertexArrayAttribBinding(microVAO, 5, 0);
		glVertexArrayAttribBinding(microVAO, 6, 0);
		glVertexArrayAttribBinding(microVAO, 7, 0);
		glVertexArrayAttribBinding(microVAO, 8, 0);
		glVertexArrayAttribBinding(microVAO, 9, 0);
		glVertexArrayAttribBinding(microVAO, 10, 0);
	}
}

WireframeDraw::~WireframeDraw() {
	freeGpuMemory();
}

WireframeDraw::WireframeDraw(WireframeDraw&& other) noexcept:
	hashSet(std::move(other.hashSet)),
	mesh(std::move(other.mesh)),
	edgeData(std::move(other.edgeData)),
	baseVBO(other.baseVBO),
	baseVAO(other.baseVAO),
	microVBO(other.microVBO),
	microVAO(other.microVAO)
{
	other.baseVBO = 0;
	other.baseVAO = 0;
	other.microVBO = 0;
	other.microVAO = 0;
}

WireframeDraw& WireframeDraw::operator=(WireframeDraw&& other) noexcept {
	if(this != &other) {
		freeGpuMemory();

		hashSet = std::move(other.hashSet);
		mesh = std::move(other.mesh);
		edgeData = std::move(other.edgeData);

		std::swap(baseVBO, other.baseVBO);
		std::swap(baseVAO, other.baseVAO);
		std::swap(microVBO, other.microVBO);
		std::swap(microVAO, other.microVAO);
	}

	return *this;
}

void WireframeDraw::freeGpuMemory() {
	glDeleteVertexArrays(1, &baseVAO);
	glDeleteVertexArrays(1, &microVAO);
	glDeleteBuffers(1, &baseVBO);
	glDeleteBuffers(1, &microVBO);

	baseVAO = 0;
	microVAO = 0;
	baseVBO = 0;
	microVBO = 0;
}

void WireframeDraw::hash_combine(size_t& seed, const float& v) {
    std::hash<float> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

size_t WireframeDraw::hash(const glm::vec3& posA, const glm::vec3& posB) {
    size_t seed = 0;
    hash_combine(seed, posA.x);
    hash_combine(seed, posA.y);
    hash_combine(seed, posA.z);
    hash_combine(seed, posB.x);
    hash_combine(seed, posB.y);
    hash_combine(seed, posB.z);
    return seed;
}

bool WireframeDraw::contains(const glm::vec3& posA, const glm::vec3& posB) const {
    return hashSet.contains(hash(posA, posB));
}

void WireframeDraw::drawBaseEdges() const {
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH);

    glLineWidth(1.0f);
    glBindVertexArray(baseVAO);
    glDrawArrays(GL_LINES, 0, edgeData.baseVertices.size());

    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
}

void WireframeDraw::drawMicroEdges() const {
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glEnable(GL_LINE_SMOOTH);

	glLineWidth(0.5f);
	glBindVertexArray(microVAO);
	glDrawArrays(GL_LINES, 0, edgeData.microVertices.size());

	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}
