#pragma once

#include <framework/mesh.h>

#include "WireframeDraw.h"

#include <filesystem>
#include <framework/opengl_includes.h>

struct MeshLoadingException final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class GPUMesh {
    static constexpr GLuint INVALID = 0xFFFFFFFF;

    GLsizei numIndices { 0 };
    GLuint ibo { INVALID };
    GLuint vbo { INVALID };
    GLuint vao { INVALID };
    GLuint uboBoneMatrices { INVALID };

    WireframeDraw wfDraw;

    void moveInto(GPUMesh&&);
    void freeGpuMemory();

public:
    Mesh cpuMesh;

    explicit GPUMesh(const Mesh& cpuMesh);
    GPUMesh(const GPUMesh&) = delete;
    GPUMesh(GPUMesh&&) noexcept;
    ~GPUMesh();

    GPUMesh& operator=(const GPUMesh&) = delete;
    GPUMesh& operator=(GPUMesh&&) noexcept;

    static std::vector<GPUMesh> loadGLTFMeshGPU(const std::filesystem::path& animFilePath, const std::filesystem::path& umeshFilePath);

    void draw(const std::vector<glm::mat4>& boneMatrices) const;
    void drawWireframe(const std::vector<glm::mat4>& bTs, const glm::mat4& mvp, float displacementScale) const;
};
