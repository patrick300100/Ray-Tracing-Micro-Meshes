#include "GPUMesh.h"
#include <framework/disable_all_warnings.h>
#include <framework/TinyGLTFLoader.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/type_ptr.inl>
#include "mesh_io_gltf.h"
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <iostream>
#include <vector>

GPUMesh::GPUMesh(const Mesh& cpuMesh): wfDraw(cpuMesh), cpuMesh(cpuMesh) {
    glCreateBuffers(1, &uboBoneMatrices);
    glNamedBufferData(uboBoneMatrices, sizeof(glm::mat4) * 50, nullptr, GL_STREAM_DRAW);

    const auto [vData, iData] = cpuMesh.allTriangles();

    glCreateBuffers(1, &ibo);
    glNamedBufferStorage(ibo, static_cast<GLsizeiptr>(iData.size() * sizeof(decltype(iData)::value_type)), iData.data(), 0);

    glCreateBuffers(1, &vbo);
    glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(vData.size() * sizeof(decltype(vData)::value_type)), vData.data(), 0);

    glCreateVertexArrays(1, &vao);

    glVertexArrayElementBuffer(vao, ibo);

    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));

    glEnableVertexArrayAttrib(vao, 0);
    glEnableVertexArrayAttrib(vao, 1);
    glEnableVertexArrayAttrib(vao, 2);
    glEnableVertexArrayAttrib(vao, 3);
    glEnableVertexArrayAttrib(vao, 4);
    glEnableVertexArrayAttrib(vao, 5);
    glEnableVertexArrayAttrib(vao, 6);
    glEnableVertexArrayAttrib(vao, 7);
    glEnableVertexArrayAttrib(vao, 8);
    glEnableVertexArrayAttrib(vao, 9);
    glEnableVertexArrayAttrib(vao, 10);
    glEnableVertexArrayAttrib(vao, 11);

    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribIFormat(vao, 2, 4, GL_INT, offsetof(Vertex, boneIndices));
    glVertexArrayAttribFormat(vao, 3, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, boneWeights));
    glVertexArrayAttribFormat(vao, 4, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, displacement));
    glVertexArrayAttribIFormat(vao, 5, 4, GL_INT, offsetof(Vertex, baseBoneIndices0));
    glVertexArrayAttribIFormat(vao, 6, 4, GL_INT, offsetof(Vertex, baseBoneIndices1));
    glVertexArrayAttribIFormat(vao, 7, 4, GL_INT, offsetof(Vertex, baseBoneIndices2));
    glVertexArrayAttribFormat(vao, 8, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, baseBoneWeights0));
    glVertexArrayAttribFormat(vao, 9, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, baseBoneWeights1));
    glVertexArrayAttribFormat(vao, 10, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, baseBoneWeights2));
    glVertexArrayAttribFormat(vao, 11, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, baryCoords));

    glVertexArrayAttribBinding(vao, 0, 0);
    glVertexArrayAttribBinding(vao, 1, 0);
    glVertexArrayAttribBinding(vao, 2, 0);
    glVertexArrayAttribBinding(vao, 3, 0);
    glVertexArrayAttribBinding(vao, 4, 0);
    glVertexArrayAttribBinding(vao, 5, 0);
    glVertexArrayAttribBinding(vao, 6, 0);
    glVertexArrayAttribBinding(vao, 7, 0);
    glVertexArrayAttribBinding(vao, 8, 0);
    glVertexArrayAttribBinding(vao, 9, 0);
    glVertexArrayAttribBinding(vao, 10, 0);
    glVertexArrayAttribBinding(vao, 11, 0);

    numIndices = static_cast<GLsizei>(3 * iData.size());
}

GPUMesh::GPUMesh(GPUMesh&& other) noexcept:
    numIndices(other.numIndices),
    ibo(other.ibo),
    vbo(other.vbo),
    vao(other.vao),
    uboBoneMatrices(other.uboBoneMatrices),
    wfDraw(std::move(other.wfDraw)),
    cpuMesh(std::move(other.cpuMesh))
{
    other.numIndices = 0;
    other.ibo = 0;
    other.vbo = 0;
    other.vao = 0;
    other.uboBoneMatrices = 0;
}

GPUMesh::~GPUMesh() {
    freeGpuMemory();
}

GPUMesh& GPUMesh::operator=(GPUMesh&& other) noexcept {
    if(this != &other) {
        freeGpuMemory();

        wfDraw = std::move(other.wfDraw);
        cpuMesh = std::move(other.cpuMesh);

        std::swap(numIndices, other.numIndices);
        std::swap(ibo, other.ibo);
        std::swap(vbo, other.vbo);
        std::swap(vao, other.vao);
        std::swap(uboBoneMatrices, other.uboBoneMatrices);
    }

    return *this;
}

std::vector<GPUMesh> GPUMesh::loadGLTFMeshGPU(const std::filesystem::path& animFilePath, const std::filesystem::path& umeshFilePath) {
    if(!std::filesystem::exists(animFilePath)) throw MeshLoadingException(fmt::format("File {} does not exist", animFilePath.string().c_str()));

    GLTFReadInfo read_micromesh;
    if (!read_gltf(umeshFilePath.string(), read_micromesh)) {
        std::cerr << "Error reading gltf file" << std::endl;
    }

    if (!read_micromesh.has_subdivision_mesh()) {
        std::cerr << "gltf file does not contain micromesh data" << std::endl;
    }

    std::vector<Mesh> subMeshes = TinyGLTFLoader(animFilePath, read_micromesh).toMesh(read_micromesh);
    std::vector<GPUMesh> gpuMeshes;
    for (const Mesh& mesh : subMeshes) { gpuMeshes.emplace_back(mesh); }

    return gpuMeshes;
}

void GPUMesh::draw(const std::vector<glm::mat4>& boneMatrices) const {
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboBoneMatrices);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(boneMatrices.size() * sizeof(glm::mat4)), boneMatrices.data());

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_INT, nullptr);
}

void GPUMesh::freeGpuMemory() {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ibo);
    glDeleteBuffers(1, &uboBoneMatrices);

    vao = 0;
    vbo = 0;
    ibo = 0;
    uboBoneMatrices = 0;
}

void GPUMesh::drawWireframe(const std::vector<glm::mat4>& bTs, const glm::mat4& mvp, const float displacementScale) const {
    glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(1, displacementScale);
    glUniform4fv(2, 1, glm::value_ptr(glm::vec4(0.235f, 0.235f, 0.235f, 1.0f)));

    wfDraw.drawBaseEdges(bTs);

    glUniform4fv(2, 1, glm::value_ptr(glm::vec4(0.435f, 0.435f, 0.435f, 0.5f)));

    wfDraw.drawMicroEdges(bTs);
}
