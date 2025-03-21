#include "GPUMesh.h"
#include <framework/disable_all_warnings.h>
#include <framework/TinyGLTFLoader.h>
#include <glm/gtc/type_ptr.inl>

#include "mesh_io_gltf.h"
#include "tangent.h"
DISABLE_WARNINGS_PUSH()
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <iostream>
#include <vector>

GPUMesh::GPUMesh(const Mesh& cpuMesh): cpuMesh(cpuMesh), wfDraw(cpuMesh) {
    //Create uniform buffer to store bone transformations
    glGenBuffers(1, &uboBoneMatrices);
    glBindBuffer(GL_UNIFORM_BUFFER, uboBoneMatrices);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) * 50, nullptr, GL_STREAM_DRAW);

    // Figure out if this mesh has texture coordinates
    m_hasTextureCoords = static_cast<bool>(cpuMesh.material.kdTexture);

    // Create VAO and bind it so subsequent creations of VBO and IBO are bound to this VAO
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // Create vertex buffer object (VBO)
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.vertices.size() * sizeof(decltype(cpuMesh.vertices)::value_type)), cpuMesh.vertices.data(), GL_STATIC_DRAW);

    // Create index buffer object (IBO)
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
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
    numIndices = static_cast<GLsizei>(3 * cpuMesh.triangles.size());
}

GPUMesh::GPUMesh(GPUMesh&& other): wfDraw(std::move(other.wfDraw))
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
    for (const Mesh& mesh : subMeshes) { gpuMeshes.emplace_back(mesh); }

    return gpuMeshes;
}

bool GPUMesh::hasTextureCoords() const
{
    return m_hasTextureCoords;
}

void GPUMesh::draw(const std::vector<glm::mat4>& boneMatrices) const {
    glBufferSubData(GL_UNIFORM_BUFFER, 0, boneMatrices.size() * sizeof(glm::mat4), boneMatrices.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboBoneMatrices);
    
    // Draw the mesh's triangles
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_INT, nullptr);
}

void GPUMesh::moveInto(GPUMesh&& other)
{
    freeGpuMemory();
    numIndices = other.numIndices;
    m_hasTextureCoords = other.m_hasTextureCoords;
    ibo = other.ibo;
    vbo = other.vbo;
    vao = other.vao;
    uboBoneMatrices = other.uboBoneMatrices;

    other.numIndices = 0;
    other.m_hasTextureCoords = other.m_hasTextureCoords;
    other.ibo = INVALID;
    other.vbo = INVALID;
    other.vao = INVALID;
    other.uboBoneMatrices = INVALID;
}

void GPUMesh::freeGpuMemory()
{
    if (vao != INVALID)
        glDeleteVertexArrays(1, &vao);
    if (vbo != INVALID)
        glDeleteBuffers(1, &vbo);
    if (ibo != INVALID)
        glDeleteBuffers(1, &ibo);
    if (uboBoneMatrices != INVALID)
        glDeleteBuffers(1, &uboBoneMatrices);
}

void GPUMesh::drawWireframe(const std::vector<glm::mat4>& bTs, const glm::mat4& mvp, const float displacementScale) const {
    glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(1, displacementScale);
    glUniform4fv(2, 1, glm::value_ptr(glm::vec4(0.235f, 0.235f, 0.235f, 1.0f)));

    wfDraw.drawBaseEdges(bTs);

    glUniform4fv(2, 1, glm::value_ptr(glm::vec4(0.435f, 0.435f, 0.435f, 0.5f)));

    wfDraw.drawMicroEdges(bTs);
}


