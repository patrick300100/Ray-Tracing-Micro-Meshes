#pragma once

#include <framework/disable_all_warnings.h>
#include <framework/mesh.h>
#include <framework/shader.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

#include <exception>
#include <filesystem>
#include <framework/opengl_includes.h>

struct MeshLoadingException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct WireframeVertex {
    glm::vec3 position;
    glm::vec3 displacement;
    glm::ivec4 boneIndices;
    glm::vec4 boneWeights;
};

class GPUMesh {
public:
    GPUMesh(const Mesh& cpuMesh, const SubdivisionMesh& umesh);
    // Cannot copy a GPU mesh because it would require reference counting of GPU resources.
    GPUMesh(const GPUMesh&) = delete;
    GPUMesh(GPUMesh&&);
    ~GPUMesh();

    // Generate a number of GPU meshes from a particular model file.
    // Multiple meshes may be generated if there are multiple sub-meshes in the file
    static std::vector<GPUMesh> loadMeshGPU(std::filesystem::path filePath, bool normalize = false);
    static std::vector<GPUMesh> loadGLTFMeshGPU(const std::filesystem::path& animFilePath, const std::filesystem::path& umeshFilePath);

    // Cannot copy a GPU mesh because it would require reference counting of GPU resources.
    GPUMesh& operator=(const GPUMesh&) = delete;
    GPUMesh& operator=(GPUMesh&&);

    bool hasTextureCoords() const;

    // Bind VAO and call glDrawElements.
    void draw(const std::vector<glm::mat4>& boneMatrices) const;

    Mesh cpuMesh;

    void drawBaseEdges(const std::vector<glm::mat4>& bTs); //Draw edges of base mesh
    void drawMicroEdges(); //Draw edges of micro mesh

private:
    void moveInto(GPUMesh&&);
    void freeGpuMemory();

private:
    static constexpr GLuint INVALID = 0xFFFFFFFF;

    GLsizei m_numIndices { 0 };
    bool m_hasTextureCoords { false };
    GLuint m_ibo { INVALID };
    GLuint m_vbo { INVALID };
    GLuint m_vao { INVALID };
    GLuint m_uboBoneMatrices { INVALID };


    std::vector<WireframeVertex> baseVerticesLine;
    GLuint buffer_wire_border { INVALID }, vao_wire_border { INVALID };

    std::vector<WireframeVertex> microVerticesLine;
    GLuint buffer_wire_inner_border { INVALID }, vao_wire_inner_border { INVALID };
};
