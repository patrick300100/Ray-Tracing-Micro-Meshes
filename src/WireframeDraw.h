#pragma once

#include <unordered_set>
#include <framework/mesh.h>
#include <framework/MeshIterator.h>
#include <framework/opengl_includes.h>

struct WireframeVertex {
  glm::vec3 position;
  glm::vec3 displacement;
  glm::ivec4 boneIndices;
  glm::vec4 boneWeights;
  size_t baseTriangleIndex;

  WireframeVertex(const glm::vec3& p, const glm::vec3& d, const glm::ivec4& bi, const glm::vec4& bw) {
    position = p;
    displacement = d;
    boneIndices = bi;
    boneWeights = bw;
  }

  WireframeVertex(const glm::vec3& p, const glm::vec3& d, const size_t bti) {
    position = p;
    displacement = d;
    baseTriangleIndex = bti;
  }
};

class WireframeDraw {
  std::unordered_set<size_t> hashSet;

  struct {
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;

    [[nodiscard]] MeshIterator begin() const {
      return {vertices, triangles, 0};
    }

    [[nodiscard]] MeshIterator end() const {
      return {vertices, triangles, triangles.size()};
    }
  } mesh;

  struct {
    std::vector<WireframeVertex> baseVertices;
    std::vector<WireframeVertex> microVertices;
  } edgeData; //Each consecutive entry creates a line. So [0] and [1] create a line, [2] and [3] create a line, etc.

  //Buffers
  GLuint baseVBO { 0 }, baseVAO { 0 };
  GLuint microVBO { 0 }, microVAO { 0 };

  static size_t hash(const glm::vec3& posA, const glm::vec3& posB);
  static void hash_combine(size_t& seed, const float& v);
  bool contains(const glm::vec3& posA, const glm::vec3& posB) const;

  void freeGpuMemory();

public:
  explicit WireframeDraw(const Mesh& m);
  WireframeDraw(const WireframeDraw&) = delete;
  WireframeDraw(WireframeDraw&& other) noexcept;
  ~WireframeDraw();

  WireframeDraw& operator=(const WireframeDraw&) = delete;
  WireframeDraw& operator=(WireframeDraw&& other) noexcept;

  void drawBaseEdges(const std::vector<glm::mat4>& bTs) const;
  void drawMicroEdges(const std::vector<glm::mat4>& bTs) const;
};
