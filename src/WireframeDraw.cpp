#include "WireframeDraw.h"
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
DISABLE_WARNINGS_POP()
#include <ranges>

WireframeDraw::WireframeDraw(const Mesh& m): hashSet(m.vertices.size()), mesh{m.vertices, m.triangles} {
    //Add base edges
    {
        auto addBaseEdge = [&](const Vertex& vA, const Vertex& vB) {
            edgeData.baseVertices.emplace_back(vA.position, vA.displacement, vA.boneIndices, vA.boneWeights);
            edgeData.baseVertices.emplace_back(vB.position, vB.displacement, vB.boneIndices, vB.boneWeights);

            hashSet.insert(hash(vA.position, vB.position));
        };

        for(const auto& [v0, v1, v2] : mesh) {
            if(!(contains(v0.position, v1.position) || contains(v1.position, v0.position))) addBaseEdge(v0, v1);
            if(!(contains(v1.position, v2.position) || contains(v2.position, v1.position))) addBaseEdge(v1, v2);
            if(!(contains(v0.position, v2.position) || contains(v2.position, v0.position))) addBaseEdge(v0, v2);
        }

    	glCreateBuffers(1, &baseVBO);
    	glNamedBufferData(baseVBO, static_cast<GLsizeiptr>(edgeData.baseVertices.size() * sizeof(WireframeVertex)), edgeData.baseVertices.data(), GL_STREAM_DRAW);

    	glCreateVertexArrays(1, &baseVAO);

    	glVertexArrayVertexBuffer(baseVAO, 0, baseVBO, 0, sizeof(WireframeVertex));

    	glVertexArrayAttribFormat(baseVAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(WireframeVertex, position));
    	glVertexArrayAttribBinding(baseVAO, 0, 0);
    	glEnableVertexArrayAttrib(baseVAO, 0);

    	glVertexArrayAttribFormat(baseVAO, 1, 3, GL_FLOAT, GL_FALSE, offsetof(WireframeVertex, displacement));
    	glVertexArrayAttribBinding(baseVAO, 1, 0);
    	glEnableVertexArrayAttrib(baseVAO, 1);
    }

    //Add micro edges
    {
        auto addMicroEdge = [&](const uVertex& vA, const uVertex& vB, size_t index) {
            edgeData.microVertices.emplace_back(vA.position, vA.displacement, index);
            edgeData.microVertices.emplace_back(vB.position, vB.displacement, index);

            hashSet.insert(hash(vA.position, vB.position));
        };

        for(const auto& [index, t] : std::views::enumerate(mesh.triangles)) {
            for(const auto& uf : t.uFaces) {
                const auto& v0 = t.uVertices[uf[0]];
                const auto& v1 = t.uVertices[uf[1]];
                const auto& v2 = t.uVertices[uf[2]];

            	//A triangle has 3 edges. Loop over each edge
            	for(const auto& [vA, vB] : std::vector<std::pair<uVertex, uVertex>>{{v0, v1}, {v1, v2}, {v0, v2}}) {
            		const auto bary = t.computeBaryCoords(mesh.vertices[t.baseVertexIndices[0]].position, mesh.vertices[t.baseVertexIndices[1]].position, mesh.vertices[t.baseVertexIndices[2]].position, (vA.position + vB.position) / 2.0f);

		            const bool onBaseEdge = glm::any(glm::epsilonEqual(bary, glm::vec3(0.0f), 0.0001f)); //Edge (of micro triangle) lies on edge of base triangle
            		const bool drawnBefore = contains(vA.position, vB.position) || contains(vB.position, vA.position); //This edge has already been drawn before

            		if(!onBaseEdge && !drawnBefore) addMicroEdge(vA, vB, index);
            	}
            }
        }

    	glCreateBuffers(1, &microVBO);
    	glNamedBufferData(microVBO, static_cast<GLsizeiptr>(edgeData.microVertices.size() * sizeof(WireframeVertex)), edgeData.microVertices.data(), GL_STREAM_DRAW);

    	glCreateVertexArrays(1, &microVAO);

    	glVertexArrayVertexBuffer(microVAO, 0, microVBO, 0, sizeof(WireframeVertex));

    	glVertexArrayAttribFormat(microVAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(WireframeVertex, position));
    	glVertexArrayAttribBinding(microVAO, 0, 0);
    	glEnableVertexArrayAttrib(microVAO, 0);

    	glVertexArrayAttribFormat(microVAO, 1, 3, GL_FLOAT, GL_FALSE, offsetof(WireframeVertex, displacement));
    	glVertexArrayAttribBinding(microVAO, 1, 0);
    	glEnableVertexArrayAttrib(microVAO, 1);
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

void WireframeDraw::drawBaseEdges(const std::vector<glm::mat4> &bTs) const {
    std::vector<WireframeVertex> newVs;
    newVs.reserve(edgeData.baseVertices.size());

    for(const auto& v : edgeData.baseVertices) {
        glm::mat4 skinMatrix = v.boneWeights.x * bTs[v.boneIndices.x] + v.boneWeights.y * bTs[v.boneIndices.y] + v.boneWeights.z * bTs[v.boneIndices.z] + v.boneWeights.w * bTs[v.boneIndices.w];

        const auto skinnedPos = skinMatrix * glm::vec4(v.position, 1.0f);
		const auto skinnedPosXYZ = glm::vec3{skinnedPos.x, skinnedPos.y, skinnedPos.z};

    	//Add small offset to avoid z-fighting
        newVs.emplace_back(skinnedPosXYZ + 0.001f * v.displacement, v.displacement, v.boneIndices, v.boneWeights);
    }

	glNamedBufferSubData(baseVBO, 0, static_cast<GLsizeiptr>(newVs.size() * sizeof(WireframeVertex)), newVs.data());

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

void WireframeDraw::drawMicroEdges(const std::vector<glm::mat4> &bTs) const {
	std::vector<WireframeVertex> newVs;
	newVs.reserve(edgeData.microVertices.size());

	for(const auto& uv : edgeData.microVertices) {
		const auto& t = mesh.triangles[uv.baseTriangleIndex];

		const auto bv0 = mesh.vertices[t.baseVertexIndices[0]];
		const auto bv1 = mesh.vertices[t.baseVertexIndices[1]];
		const auto bv2 = mesh.vertices[t.baseVertexIndices[2]];

		auto baryCoords = t.computeBaryCoords(bv0.position, bv1.position, bv2.position, uv.position);

		auto bv0SkinMatrix = bv0.boneWeights.x * bTs[bv0.boneIndices.x] + bv0.boneWeights.y * bTs[bv0.boneIndices.y] + bv0.boneWeights.z * bTs[bv0.boneIndices.z] + bv0.boneWeights.w * bTs[bv0.boneIndices.w];
		auto bv1SkinMatrix = bv1.boneWeights.x * bTs[bv1.boneIndices.x] + bv1.boneWeights.y * bTs[bv1.boneIndices.y] + bv1.boneWeights.z * bTs[bv1.boneIndices.z] + bv1.boneWeights.w * bTs[bv1.boneIndices.w];
		auto bv2SkinMatrix = bv2.boneWeights.x * bTs[bv2.boneIndices.x] + bv2.boneWeights.y * bTs[bv2.boneIndices.y] + bv2.boneWeights.z * bTs[bv2.boneIndices.z] + bv2.boneWeights.w * bTs[bv2.boneIndices.w];

		//For some reason we need to take the transpose
		bv0SkinMatrix = glm::transpose(bv0SkinMatrix);
		bv1SkinMatrix = glm::transpose(bv1SkinMatrix);
		bv2SkinMatrix = glm::transpose(bv2SkinMatrix);

		const auto interpolatedSkinMatrix = baryCoords.x * bv0SkinMatrix + baryCoords.y * bv1SkinMatrix + baryCoords.z * bv2SkinMatrix;
		const auto uvNewPos = glm::vec4(uv.position, 1.0f) * interpolatedSkinMatrix + glm::vec4(uv.displacement, 1.0f) * glm::vec4(baryCoords.x * bv0.normal + baryCoords.y * bv1.normal + baryCoords.z * bv2.normal, 0.0f) * interpolatedSkinMatrix;
		const auto uvNewPosXYZ = glm::vec3{uvNewPos.x, uvNewPos.y, uvNewPos.z};

		//Add small offset to avoid z-fighting
		newVs.emplace_back(uvNewPosXYZ + 0.001f * uv.displacement, uv.displacement, uv.baseTriangleIndex);
	}

	glNamedBufferSubData(microVBO, 0, static_cast<GLsizeiptr>(newVs.size() * sizeof(WireframeVertex)), newVs.data());

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
