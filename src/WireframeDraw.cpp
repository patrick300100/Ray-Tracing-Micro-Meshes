#include "WireframeDraw.h"

#include <iostream>
#include <sstream>
#include <unordered_map>
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
DISABLE_WARNINGS_POP()
#include <ranges>

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

private:
	template <class T>
	static void hash_combine(std::size_t& seed, const T& v) {
		std::hash<T> hasher;
		seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}
};

WireframeDraw::WireframeDraw(const Mesh& m) {
	//Create data structures for creating VBO's and IBO's
	std::unordered_map<Vertex, uint32_t, VertexHash> vertexCache(2 * m.vertices.size());
	std::unordered_set<size_t> edgeCache(3 * m.vertices.size());

	std::vector<uint32_t> baseIndices;
	std::vector<uint32_t> microIndices;

	std::vector<Vertex> vs;

	auto addIndex = [&](std::vector<uint32_t>& indexList, const Vertex& v) {
		if (auto iter = vertexCache.find(v); iter != vertexCache.end()) {
			//Already visited this vertex? Reuse it!
			indexList.emplace_back(iter->second);
		} else {
			//New vertex? Create it and store it in the vertex cache.
			vertexCache[v] = vs.size();
			indexList.emplace_back(vs.size());

			vs.emplace_back(v);
		}
	};

	for(const auto& t : m.triangles) {
		const auto& bv0 = m.vertices[t.baseVertexIndices[0]];
		const auto& bv1 = m.vertices[t.baseVertexIndices[1]];
		const auto& bv2 = m.vertices[t.baseVertexIndices[2]];

		for(const auto& uf : t.uFaces) {
			const auto& uv0 = t.uVertices[uf[0]];
			const auto& uv1 = t.uVertices[uf[1]];
			const auto& uv2 = t.uVertices[uf[2]];

			//Loop over each edge of the (micro) triangle
			for(const auto& [uvA, uvB] : std::vector<std::pair<uVertex, uVertex>>{{uv0, uv1}, {uv1, uv2}, {uv0, uv2}}) {
				//Barycentric coordinate of position in the middle of the edge
				const auto baryMiddle = Triangle::computeBaryCoords(
					m.vertices[t.baseVertexIndices[0]].position,
					m.vertices[t.baseVertexIndices[1]].position,
					m.vertices[t.baseVertexIndices[2]].position,
					(uvA.position + uvB.position) / 2.0f
				);

				const bool onBaseEdge = glm::any(glm::epsilonEqual(baryMiddle, glm::vec3(0.0f), 0.0001f)); //Edge (of micro triangle) lies on edge of base triangle
				const bool drawnBefore = edgeCache.contains(hash(uvA.position, uvB.position)) || edgeCache.contains(hash(uvB.position, uvA.position)); //This edge has already been drawn before

				const auto hashValue = hash(uvA.position, uvB.position);

				for(const auto& uv : {uvA, uvB}) {
					const auto v = Vertex {
						.position = uv.position,
						.displacement = uv.displacement,
						.baseBoneIndices0 = bv0.boneIndices,
						.baseBoneIndices1 = bv1.boneIndices,
						.baseBoneIndices2 = bv2.boneIndices,
						.baseBoneWeights0 = bv0.boneWeights,
						.baseBoneWeights1 = bv1.boneWeights,
						.baseBoneWeights2 = bv2.boneWeights,
						.baryCoords = Triangle::computeBaryCoords(bv0.position, bv1.position, bv2.position, uv.position)
					};

					if(!drawnBefore) {
						if(onBaseEdge) {
							edgeCache.insert(hashValue);
							addIndex(baseIndices, v);
						}
						else {
							edgeCache.insert(hashValue);
							addIndex(microIndices, v);
						}
					}
				}
			}
		}
	}

	glCreateBuffers(1, &vbo);
	glNamedBufferData(vbo, static_cast<GLsizeiptr>(vs.size() * sizeof(Vertex)), vs.data(), GL_STATIC_DRAW);

	//Create buffers for base edges
	{
		glCreateBuffers(1, &baseIBO);
		glNamedBufferStorage(baseIBO, static_cast<GLsizeiptr>(baseIndices.size() * sizeof(decltype(baseIndices)::value_type)), baseIndices.data(), 0);

		glCreateVertexArrays(1, &baseVAO);

		glVertexArrayElementBuffer(baseVAO, baseIBO);

		glVertexArrayVertexBuffer(baseVAO, 0, vbo, 0, sizeof(Vertex));

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

		baseNumIndices = static_cast<GLsizei>(baseIndices.size());
	}

	//Create buffers for micro edges
	{
		glCreateBuffers(1, &microIBO);
		glNamedBufferStorage(microIBO, static_cast<GLsizeiptr>(microIndices.size() * sizeof(decltype(microIndices)::value_type)), microIndices.data(), 0);

		glCreateVertexArrays(1, &microVAO);

		glVertexArrayElementBuffer(microVAO, microIBO);

		glVertexArrayVertexBuffer(microVAO, 0, vbo, 0, sizeof(Vertex));

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

		microNumIndices = static_cast<GLsizei>(microIndices.size());
	}
}

WireframeDraw::~WireframeDraw() {
	freeGpuMemory();
}

WireframeDraw::WireframeDraw(WireframeDraw&& other) noexcept:
	vbo(other.vbo),
	baseVAO(other.baseVAO),
	baseIBO(other.baseIBO),
	baseNumIndices(other.baseNumIndices),
	microVAO(other.microVAO),
	microIBO(other.microIBO),
	microNumIndices(other.microNumIndices)
{
	other.vbo = 0;
	other.baseVAO = 0;
	other.baseIBO = 0;
	other.baseNumIndices = 0;
	other.microVAO = 0;
	other.microIBO = 0;
	other.microNumIndices = 0;
}

WireframeDraw& WireframeDraw::operator=(WireframeDraw&& other) noexcept {
	if(this != &other) {
		freeGpuMemory();

		std::swap(vbo, other.vbo);
		std::swap(baseVAO, other.baseVAO);
		std::swap(baseIBO, other.baseIBO);
		std::swap(microVAO, other.microVAO);
		std::swap(microIBO, other.microIBO);

		baseNumIndices = other.baseNumIndices;
		microNumIndices = other.microNumIndices;

		other.baseNumIndices = 0;
		other.microNumIndices = 0;
	}

	return *this;
}

void WireframeDraw::freeGpuMemory() {
	glDeleteVertexArrays(1, &baseVAO);
	glDeleteVertexArrays(1, &microVAO);
	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &baseIBO);
	glDeleteBuffers(1, &microIBO);

	baseVAO = 0;
	microVAO = 0;
	vbo = 0;
	baseIBO = 0;
	microIBO = 0;
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

void WireframeDraw::drawBaseEdges() const {
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH);

    glLineWidth(1.0f);
    glBindVertexArray(baseVAO);
	glDrawElements(GL_LINES, baseNumIndices, GL_UNSIGNED_INT, nullptr);

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
	glDrawElements(GL_LINES, microNumIndices, GL_UNSIGNED_INT, nullptr);

	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}
