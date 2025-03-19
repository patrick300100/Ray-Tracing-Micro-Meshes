#include "MeshIterator.h"

MeshIterator::MeshIterator(const std::vector<Vertex>& v, const std::vector<Triangle>& t, const size_t index): vertices(v), triangles(t), triangleIndex(index) {}

std::array<const Vertex, 3> MeshIterator::operator*() const {
    const Triangle& t = triangles[triangleIndex];
    return { vertices[t.baseVertexIndices[0]], vertices[t.baseVertexIndices[1]], vertices[t.baseVertexIndices[2]] };
}

MeshIterator& MeshIterator::operator++() {
    ++triangleIndex;
    return *this;
}

bool MeshIterator::operator!=(const MeshIterator& other) const {
    return triangleIndex != other.triangleIndex;
}

