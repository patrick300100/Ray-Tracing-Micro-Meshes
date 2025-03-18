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


    std::vector<float> wire_buffer_inner;

    wire_buffer_inner.reserve(umesh.micro_fn);

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

	glLineWidth(1.0f);
	glBindVertexArray(vao_wire_border);
	glDrawArrays(GL_LINES, 0, baseVerticesLine.size());

	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}

