#include "mesh.h"
#include <framework/disable_all_warnings.h>
#include <framework/TinyGLTFLoader.h>

#include "mesh_io_gltf.h"
#include "tangent.h"
DISABLE_WARNINGS_PUSH()
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <iostream>
#include <vector>

GPUMesh::GPUMesh(const Mesh& cpuMesh, const SubdivisionMesh& umesh): cpuMesh(cpuMesh) {
    //Create uniform buffer to store bone transformations
    glGenBuffers(1, &m_uboBoneMatrices);
    glBindBuffer(GL_UNIFORM_BUFFER, m_uboBoneMatrices);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) * 50, nullptr, GL_STREAM_DRAW);

    // Figure out if this mesh has texture coordinates
    m_hasTextureCoords = static_cast<bool>(cpuMesh.material.kdTexture);

    // Create VAO and bind it so subsequent creations of VBO and IBO are bound to this VAO
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // Create vertex buffer object (VBO)
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.vertices.size() * sizeof(decltype(cpuMesh.vertices)::value_type)), cpuMesh.vertices.data(), GL_STATIC_DRAW);

    // Create index buffer object (IBO)
    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.baseTriangleIndices.size() * sizeof(decltype(cpuMesh.baseTriangleIndices)::value_type)), cpuMesh.baseTriangleIndices.data(), GL_STATIC_DRAW);

    // Tell OpenGL that we will be using vertex attributes 0, 1, 2, 3, and 4.
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    // We tell OpenGL what each vertex looks like and how they are mapped to the shader (location = ...).
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, boneIndices));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, boneWeights));
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, displacement));
    // Reuse all attributes for each instance
    glVertexAttribDivisor(0, 0);
    glVertexAttribDivisor(1, 0);
    glVertexAttribDivisor(2, 0);
    glVertexAttribDivisor(3, 0);
    glVertexAttribDivisor(4, 0);
    glVertexAttribDivisor(5, 0);

    // Each triangle has 3 vertices.
    m_numIndices = static_cast<GLsizei>(3 * cpuMesh.triangles.size());







	baseVerticesLine.reserve(sizeof(WireframeVertex) * cpuMesh.triangles.size() * 6);
	microVerticesLine.reserve(sizeof(WireframeVertex) * cpuMesh.triangles.size() * 6); //TODO not accurate. Look at it again

	for(const auto& t : cpuMesh.triangles) {
		const auto& v0 = cpuMesh.vertices[t.baseVertexIndices[0]];
		const auto& v1 = cpuMesh.vertices[t.baseVertexIndices[1]];
		const auto& v2 = cpuMesh.vertices[t.baseVertexIndices[2]];

		baseVerticesLine.emplace_back(glm::vec3{v0.position.x, v0.position.y, v0.position.z}, glm::vec3{v0.displacement.x, v0.displacement.y, v0.displacement.z}, v0.boneIndices, v0.boneWeights);
		baseVerticesLine.emplace_back(glm::vec3{v1.position.x, v1.position.y, v1.position.z}, glm::vec3{v1.displacement.x, v1.displacement.y, v1.displacement.z}, v1.boneIndices, v1.boneWeights);
		baseVerticesLine.emplace_back(glm::vec3{v1.position.x, v1.position.y, v1.position.z}, glm::vec3{v1.displacement.x, v1.displacement.y, v1.displacement.z}, v1.boneIndices, v1.boneWeights);
		baseVerticesLine.emplace_back(glm::vec3{v2.position.x, v2.position.y, v2.position.z}, glm::vec3{v2.displacement.x, v2.displacement.y, v2.displacement.z}, v2.boneIndices, v2.boneWeights);
		baseVerticesLine.emplace_back(glm::vec3{v0.position.x, v0.position.y, v0.position.z}, glm::vec3{v0.displacement.x, v0.displacement.y, v0.displacement.z}, v0.boneIndices, v0.boneWeights);
		baseVerticesLine.emplace_back(glm::vec3{v2.position.x, v2.position.y, v2.position.z}, glm::vec3{v2.displacement.x, v2.displacement.y, v2.displacement.z}, v2.boneIndices, v2.boneWeights);
	}

	glGenBuffers(1, &buffer_wire_border);
	glBindBuffer(GL_ARRAY_BUFFER, buffer_wire_border);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(baseVerticesLine.size() * sizeof(WireframeVertex)), baseVerticesLine.data(), GL_STREAM_DRAW);

	glGenVertexArrays(1, &vao_wire_border);
	glBindVertexArray(vao_wire_border);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WireframeVertex), (void*)offsetof(WireframeVertex, position));
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WireframeVertex), (void*)offsetof(WireframeVertex, displacement));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(WireframeVertex), (void*)offsetof(WireframeVertex, boneIndices));
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(WireframeVertex), (void*)offsetof(WireframeVertex, boneWeights));
	glEnableVertexAttribArray(3);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//Setup buffers for drawing micro vertices
    {
		for(const auto& t : cpuMesh.triangles) {
			for(const auto& uf : t.uFaces) {
				const auto& v0 = t.uVertices[uf[0]];
				const auto& v1 = t.uVertices[uf[1]];
				const auto& v2 = t.uVertices[uf[2]];

				microVerticesLine.emplace_back(glm::vec3{v0.position.x, v0.position.y, v0.position.z}, glm::vec3{v0.displacement.x, v0.displacement.y, v0.displacement.z});
				microVerticesLine.emplace_back(glm::vec3{v1.position.x, v1.position.y, v1.position.z}, glm::vec3{v1.displacement.x, v1.displacement.y, v1.displacement.z});
				microVerticesLine.emplace_back(glm::vec3{v1.position.x, v1.position.y, v1.position.z}, glm::vec3{v1.displacement.x, v1.displacement.y, v1.displacement.z});
				microVerticesLine.emplace_back(glm::vec3{v2.position.x, v2.position.y, v2.position.z}, glm::vec3{v2.displacement.x, v2.displacement.y, v2.displacement.z});
				microVerticesLine.emplace_back(glm::vec3{v0.position.x, v0.position.y, v0.position.z}, glm::vec3{v0.displacement.x, v0.displacement.y, v0.displacement.z});
				microVerticesLine.emplace_back(glm::vec3{v2.position.x, v2.position.y, v2.position.z}, glm::vec3{v2.displacement.x, v2.displacement.y, v2.displacement.z});
			}
		}

    	glGenBuffers(1, &buffer_wire_inner_border);
    	glBindBuffer(GL_ARRAY_BUFFER, buffer_wire_inner_border);
    	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(microVerticesLine.size() * sizeof(WireframeVertex)), microVerticesLine.data(), GL_STREAM_DRAW);

    	glGenVertexArrays(1, &vao_wire_inner_border);
    	glBindVertexArray(vao_wire_inner_border);

    	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WireframeVertex), (void*)offsetof(WireframeVertex, position));
    	glEnableVertexAttribArray(0);

    	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WireframeVertex), (void*)offsetof(WireframeVertex, displacement));
    	glEnableVertexAttribArray(1);

    	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(WireframeVertex), (void*)offsetof(WireframeVertex, boneIndices));
    	glEnableVertexAttribArray(2);

    	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(WireframeVertex), (void*)offsetof(WireframeVertex, boneWeights));
    	glEnableVertexAttribArray(3);

    	glBindVertexArray(0);
    	glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

GPUMesh::GPUMesh(GPUMesh&& other)
{
    moveInto(std::move(other));
}

GPUMesh::~GPUMesh()
{
    freeGpuMemory();
}

GPUMesh& GPUMesh::operator=(GPUMesh&& other)
{
    moveInto(std::move(other));
    return *this;
}

// std::vector<GPUMesh> GPUMesh::loadMeshGPU(std::filesystem::path filePath, bool normalize) {
//     if (!std::filesystem::exists(filePath))
//         throw MeshLoadingException(fmt::format("File {} does not exist", filePath.string().c_str()));
//
//     // Generate GPU-side meshes for all sub-meshes
//     std::vector<Mesh> subMeshes = loadMesh(filePath, normalize);
//     std::vector<GPUMesh> gpuMeshes;
//     //for (const Mesh& mesh : subMeshes) { gpuMeshes.emplace_back(mesh); }
//
//     return gpuMeshes;
// }

std::vector<GPUMesh> GPUMesh::loadGLTFMeshGPU(const std::filesystem::path& animFilePath, const std::filesystem::path& umeshFilePath) {
    if(!std::filesystem::exists(animFilePath)) throw MeshLoadingException(fmt::format("File {} does not exist", animFilePath.string().c_str()));

    GLTFReadInfo read_micromesh;
    if (!read_gltf(umeshFilePath.string(), read_micromesh)) {
        std::cerr << "Error reading gltf file" << std::endl;
    }

    if (!read_micromesh.has_subdivision_mesh()) {
        std::cerr << "gltf file does not contain micromesh data" << std::endl;
    }

    // Generate GPU-side meshes for all sub-meshes
    std::vector<Mesh> subMeshes = TinyGLTFLoader(animFilePath, read_micromesh).toMesh(read_micromesh);
    std::vector<GPUMesh> gpuMeshes;
    for (const Mesh& mesh : subMeshes) { gpuMeshes.emplace_back(mesh, mesh.umesh); }

    return gpuMeshes;
}

bool GPUMesh::hasTextureCoords() const
{
    return m_hasTextureCoords;
}

void GPUMesh::draw(const std::vector<glm::mat4>& boneMatrices) const {
    glBufferSubData(GL_UNIFORM_BUFFER, 0, boneMatrices.size() * sizeof(glm::mat4), boneMatrices.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_uboBoneMatrices);
    
    // Draw the mesh's triangles
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_numIndices, GL_UNSIGNED_INT, nullptr);
}

void GPUMesh::moveInto(GPUMesh&& other)
{
    freeGpuMemory();
    m_numIndices = other.m_numIndices;
    m_hasTextureCoords = other.m_hasTextureCoords;
    m_ibo = other.m_ibo;
    m_vbo = other.m_vbo;
    m_vao = other.m_vao;
    m_uboBoneMatrices = other.m_uboBoneMatrices;

    other.m_numIndices = 0;
    other.m_hasTextureCoords = other.m_hasTextureCoords;
    other.m_ibo = INVALID;
    other.m_vbo = INVALID;
    other.m_vao = INVALID;
    other.m_uboBoneMatrices = INVALID;
}

void GPUMesh::freeGpuMemory()
{
    if (m_vao != INVALID)
        glDeleteVertexArrays(1, &m_vao);
    if (m_vbo != INVALID)
        glDeleteBuffers(1, &m_vbo);
    if (m_ibo != INVALID)
        glDeleteBuffers(1, &m_ibo);
    if (m_uboBoneMatrices != INVALID)
        glDeleteBuffers(1, &m_uboBoneMatrices);
}

void GPUMesh::drawBaseEdges(const std::vector<glm::mat4>& bTs) {
	std::vector<WireframeVertex> newVs;
	newVs.reserve(baseVerticesLine.size());

	for(const auto& v : baseVerticesLine) {
		glm::mat4 skinMatrix = v.boneWeights.x * bTs[v.boneIndices.x] +
			v.boneWeights.y * bTs[v.boneIndices.y] +
			v.boneWeights.z * bTs[v.boneIndices.z] +
			v.boneWeights.w * bTs[v.boneIndices.w];

		const auto skinnedPos = skinMatrix * glm::vec4(v.position, 1.0f);
		newVs.emplace_back(glm::vec3{skinnedPos.x, skinnedPos.y, skinnedPos.z}, v.displacement, v.boneIndices, v.boneWeights);
	}

	glBindBuffer(GL_ARRAY_BUFFER, buffer_wire_border);
	glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(newVs.size() * sizeof(WireframeVertex)), newVs.data());
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glEnable(GL_LINE_SMOOTH);

	glLineWidth(3.0f);
	glBindVertexArray(vao_wire_border);
	glDrawArrays(GL_LINES, 0, baseVerticesLine.size());

	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}

void GPUMesh::drawMicroEdges(const std::vector<glm::mat4>& bTs) {
	std::vector<WireframeVertex> newVs;
	newVs.reserve(microVerticesLine.size());

	for(const auto& t : cpuMesh.triangles) {
		const auto bv0 = cpuMesh.vertices[t.baseVertexIndices[0]];
		const auto bv1 = cpuMesh.vertices[t.baseVertexIndices[1]];
		const auto bv2 = cpuMesh.vertices[t.baseVertexIndices[2]];

		auto baryCoords = t.baryCoords(bv0.position, bv1.position, bv2.position);

		auto bv0SkinMatrix = bv0.boneWeights.x * bTs[bv0.boneIndices.x] + bv0.boneWeights.y * bTs[bv0.boneIndices.y] + bv0.boneWeights.z * bTs[bv0.boneIndices.z] + bv0.boneWeights.w * bTs[bv0.boneIndices.w];
		auto bv1SkinMatrix = bv1.boneWeights.x * bTs[bv1.boneIndices.x] + bv1.boneWeights.y * bTs[bv1.boneIndices.y] + bv1.boneWeights.z * bTs[bv1.boneIndices.z] + bv1.boneWeights.w * bTs[bv1.boneIndices.w];
		auto bv2SkinMatrix = bv2.boneWeights.x * bTs[bv2.boneIndices.x] + bv2.boneWeights.y * bTs[bv2.boneIndices.y] + bv2.boneWeights.z * bTs[bv2.boneIndices.z] + bv2.boneWeights.w * bTs[bv2.boneIndices.w];

		//For some reason we need to take the transpose
		bv0SkinMatrix = glm::transpose(bv0SkinMatrix);
		bv1SkinMatrix = glm::transpose(bv1SkinMatrix);
		bv2SkinMatrix = glm::transpose(bv2SkinMatrix);

		for(const auto& uf : t.uFaces) {
			const auto& v0 = t.uVertices[uf[0]];
			const auto& v0BC = baryCoords[uf[0]];
			const auto& v1 = t.uVertices[uf[1]];
			const auto& v1BC = baryCoords[uf[1]];
			const auto& v2 = t.uVertices[uf[2]];
			const auto& v2BC = baryCoords[uf[2]];

			const auto interpolatedSkinMatrixV0 = v0BC.x * bv0SkinMatrix + v0BC.y * bv1SkinMatrix + v0BC.z * bv2SkinMatrix;
			const auto interpolatedSkinMatrixV1 = v1BC.x * bv0SkinMatrix + v1BC.y * bv1SkinMatrix + v1BC.z * bv2SkinMatrix;
			const auto interpolatedSkinMatrixV2 = v2BC.x * bv0SkinMatrix + v2BC.y * bv1SkinMatrix + v2BC.z * bv2SkinMatrix;

			const auto v0Temp1 = glm::vec4(v0.position, 1.0f) * interpolatedSkinMatrixV0;
			const auto v0Temp2 = glm::vec4(v0.displacement, 1.0f) * glm::vec4(v0BC.x * bv0.normal + v0BC.y * bv1.normal + v0BC.z * bv2.normal, 0.0f) * interpolatedSkinMatrixV0;

			const auto v0NewPos = v0Temp1 + v0Temp2;
			const auto v1NewPos = glm::vec4(v1.position, 1.0f) * interpolatedSkinMatrixV1 + glm::vec4(v1.displacement, 1.0f) * glm::vec4(v1BC.x * bv0.normal + v1BC.y * bv1.normal + v1BC.z * bv2.normal, 0.0f) * interpolatedSkinMatrixV1;
			const auto v2NewPos = glm::vec4(v2.position, 1.0f) * interpolatedSkinMatrixV2 + glm::vec4(v2.displacement, 1.0f) * glm::vec4(v2BC.x * bv0.normal + v2BC.y * bv1.normal + v2BC.z * bv2.normal, 0.0f) * interpolatedSkinMatrixV2;

			const auto v0NewPosXYZ = glm::vec3{v0NewPos.x / v0NewPos.w, v0NewPos.y / v0NewPos.w, v0NewPos.z / v0NewPos.w};
			const auto v1NewPosXYZ = glm::vec3{v1NewPos.x / v1NewPos.w, v1NewPos.y / v1NewPos.w, v1NewPos.z / v1NewPos.w};
			const auto v2NewPosXYZ = glm::vec3{v2NewPos.x / v2NewPos.w, v2NewPos.y / v2NewPos.w, v2NewPos.z / v2NewPos.w};

			newVs.emplace_back(v0NewPosXYZ, v0.displacement);
			newVs.emplace_back(v1NewPosXYZ, v1.displacement);
			newVs.emplace_back(v1NewPosXYZ, v1.displacement);
			newVs.emplace_back(v2NewPosXYZ, v2.displacement);
			newVs.emplace_back(v0NewPosXYZ, v0.displacement);
			newVs.emplace_back(v2NewPosXYZ, v2.displacement);
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, buffer_wire_inner_border);
	glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(newVs.size() * sizeof(WireframeVertex)), newVs.data());
	glBindBuffer(GL_ARRAY_BUFFER, 0);


	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glEnable(GL_LINE_SMOOTH);

	glLineWidth(0.2f);
	glBindVertexArray(vao_wire_inner_border);
	glDrawArrays(GL_LINES, 0, microVerticesLine.size());

	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}


