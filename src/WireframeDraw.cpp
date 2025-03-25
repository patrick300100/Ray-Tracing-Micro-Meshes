#include "WireframeDraw.h"
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
DISABLE_WARNINGS_POP()
#include <ranges>

WireframeDraw::WireframeDraw(const Mesh& m): hashSet(m.vertices.size()), mesh{m.vertices, m.triangles} {
    //Add base edges
    {
        auto addBaseEdge = [&](const Vertex& vA, const Vertex& vB) {
            edgeData.baseVertices.emplace_back(vA);
            edgeData.baseVertices.emplace_back(vB);

            hashSet.insert(hash(vA.position, vB.position));
        };

        for(const auto& [v0, v1, v2] : mesh) {
            if(!(contains(v0.position, v1.position) || contains(v1.position, v0.position))) addBaseEdge(v0, v1);
            if(!(contains(v1.position, v2.position) || contains(v2.position, v1.position))) addBaseEdge(v1, v2);
            if(!(contains(v0.position, v2.position) || contains(v2.position, v0.position))) addBaseEdge(v0, v2);
        }

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

    //Add micro edges
    {
        auto addMicroEdge = [&](const Vertex& vA, const Vertex& vB) {
            edgeData.microVertices.emplace_back(vA);
            edgeData.microVertices.emplace_back(vB);

            hashSet.insert(hash(vA.position, vB.position));
        };

        for(const auto& [index, t] : std::views::enumerate(mesh.triangles)) {
			const auto& bv0 = mesh.vertices[t.baseVertexIndices[0]];
			const auto& bv1 = mesh.vertices[t.baseVertexIndices[1]];
			const auto& bv2 = mesh.vertices[t.baseVertexIndices[2]];

            for(const auto& uf : t.uFaces) {
                const auto& v0 = t.uVertices[uf[0]];
                const auto& v1 = t.uVertices[uf[1]];
                const auto& v2 = t.uVertices[uf[2]];

            	//A triangle has 3 edges. Loop over each edge
            	for(const auto& [vA, vB] : std::vector<std::pair<uVertex, uVertex>>{{v0, v1}, {v1, v2}, {v0, v2}}) {
            		const auto bary = t.computeBaryCoords(mesh.vertices[t.baseVertexIndices[0]].position, mesh.vertices[t.baseVertexIndices[1]].position, mesh.vertices[t.baseVertexIndices[2]].position, (vA.position + vB.position) / 2.0f);

		            const bool onBaseEdge = glm::any(glm::epsilonEqual(bary, glm::vec3(0.0f), 0.0001f)); //Edge (of micro triangle) lies on edge of base triangle
            		const bool drawnBefore = contains(vA.position, vB.position) || contains(vB.position, vA.position); //This edge has already been drawn before

            		if(!onBaseEdge && !drawnBefore) {
            			const auto bc1 = t.computeBaryCoords(bv0.position, bv1.position, bv2.position, vA.position);
            			const auto bc2 = t.computeBaryCoords(bv0.position, bv1.position, bv2.position, vB.position);

            			addMicroEdge(
            				Vertex{.position = vA.position, .displacement = vA.displacement, .baseBoneIndices0 = bv0.boneIndices, .baseBoneIndices1 = bv1.boneIndices, .baseBoneIndices2 = bv2.boneIndices, .baseBoneWeights0 = bv0.boneWeights, .baseBoneWeights1 = bv1.boneWeights, .baseBoneWeights2 = bv2.boneWeights, .baryCoords = bc1},
            				Vertex{.position = vB.position, .displacement = vB.displacement, .baseBoneIndices0 = bv0.boneIndices, .baseBoneIndices1 = bv1.boneIndices, .baseBoneIndices2 = bv2.boneIndices, .baseBoneWeights0 = bv0.boneWeights, .baseBoneWeights1 = bv1.boneWeights, .baseBoneWeights2 = bv2.boneWeights, .baryCoords = bc2}
            			);
            		}
            	}
            }
        }

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

void WireframeDraw::drawBaseEdges(const std::vector<glm::mat4>& bTs) const {
    std::vector<Vertex> newVs;
    newVs.reserve(edgeData.baseVertices.size());

    for(auto v : edgeData.baseVertices) {
        glm::mat4 skinMatrix = v.boneWeights.x * bTs[v.boneIndices.x] + v.boneWeights.y * bTs[v.boneIndices.y] + v.boneWeights.z * bTs[v.boneIndices.z] + v.boneWeights.w * bTs[v.boneIndices.w];

        const auto skinnedPos = skinMatrix * glm::vec4(v.position, 1.0f);
		const auto skinnedPosXYZ = glm::vec3{skinnedPos.x, skinnedPos.y, skinnedPos.z};

    	v.position = skinnedPosXYZ + 0.001f * v.displacement; //Add small offset to avoid z-fighting

        newVs.emplace_back(v);
    }

	glNamedBufferSubData(baseVBO, 0, static_cast<GLsizeiptr>(newVs.size() * sizeof(Vertex)), newVs.data());

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

void WireframeDraw::drawMicroEdges(std::vector<glm::mat4> bTs) const {
	for(auto& bT : bTs) bT = glm::transpose(bT); //For some reason we need to take the transpose

	std::vector<Vertex> newVs;
	newVs.reserve(edgeData.microVertices.size());

	for(const auto& uv : edgeData.microVertices) {
		auto bv0SkinMatrix = uv.baseBoneWeights0.x * bTs[uv.baseBoneIndices0.x] + uv.baseBoneWeights0.y * bTs[uv.baseBoneIndices0.y] + uv.baseBoneWeights0.z * bTs[uv.baseBoneIndices0.z] + uv.baseBoneWeights0.w * bTs[uv.baseBoneIndices0.w];
		auto bv1SkinMatrix = uv.baseBoneWeights1.x * bTs[uv.baseBoneIndices1.x] + uv.baseBoneWeights1.y * bTs[uv.baseBoneIndices1.y] + uv.baseBoneWeights1.z * bTs[uv.baseBoneIndices1.z] + uv.baseBoneWeights1.w * bTs[uv.baseBoneIndices1.w];
		auto bv2SkinMatrix = uv.baseBoneWeights2.x * bTs[uv.baseBoneIndices2.x] + uv.baseBoneWeights2.y * bTs[uv.baseBoneIndices2.y] + uv.baseBoneWeights2.z * bTs[uv.baseBoneIndices2.z] + uv.baseBoneWeights2.w * bTs[uv.baseBoneIndices2.w];

		const auto interpolatedSkinMatrix = uv.baryCoords.x * bv0SkinMatrix + uv.baryCoords.y * bv1SkinMatrix + uv.baryCoords.z * bv2SkinMatrix;
		const auto uvNewPos = glm::vec4(uv.position, 1.0f) * interpolatedSkinMatrix;
		const auto uvNewPosXYZ = glm::vec3{uvNewPos.x, uvNewPos.y, uvNewPos.z};

		//Add small offset to avoid z-fighting
		newVs.emplace_back(uvNewPosXYZ + 0.001f * uv.displacement, uv.displacement);
	}

	glNamedBufferSubData(microVBO, 0, static_cast<GLsizeiptr>(newVs.size() * sizeof(Vertex)), newVs.data());

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
