#pragma once

struct TriangleData {
    glm::uvec3 vIndices;
    int nRows;
    int subDivisionLevel;
    int displacementOffset;
    int minMaxOffset;
};
