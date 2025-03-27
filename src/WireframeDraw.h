#pragma once

#include <unordered_set>
#include <framework/mesh.h>
#include <framework/MeshIterator.h>
#include <framework/opengl_includes.h>

class WireframeDraw {
  GLuint vbo { 0 }; //VBO is shared between base and micro edges

  GLuint baseVAO { 0 }, baseIBO { 0 };
  GLsizei baseNumIndices { 0 };

  GLuint microVAO { 0 }, microIBO { 0 };
  GLsizei microNumIndices { 0 };

  static size_t hash(const glm::vec3& posA, const glm::vec3& posB);
  static void hash_combine(size_t& seed, const float& v);

  void freeGpuMemory();

public:
  explicit WireframeDraw(const Mesh& m);
  WireframeDraw(const WireframeDraw&) = delete;
  WireframeDraw(WireframeDraw&& other) noexcept;
  ~WireframeDraw();

  WireframeDraw& operator=(const WireframeDraw&) = delete;
  WireframeDraw& operator=(WireframeDraw&& other) noexcept;

  void drawBaseEdges() const;
  void drawMicroEdges() const;
};
