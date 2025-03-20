#pragma once

#include "mesh.h"
#include <vector>

/**
 * This iterator allows me to immediately loop over each 3 vertex triangles, instead of looping over the triangle
 * vector and fetch the 3 vertices manually.
 */
class MeshIterator {
    const std::vector<Vertex>& vertices;
    const std::vector<Triangle>& triangles;
    size_t triangleIndex;

public:
    MeshIterator(const std::vector<Vertex>& v, const std::vector<Triangle>& t, size_t index);

    std::array<const Vertex, 3> operator*() const;
    MeshIterator& operator++();
    bool operator!=(const MeshIterator& other) const;
};
