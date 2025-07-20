#pragma once
#include <glm/glm.hpp>

struct Vertex2D {
    glm::vec2 position;
    glm::uvec2 coordinates;
};

struct Edge2D {
    Vertex2D start;
    Vertex2D end;

    [[nodiscard]] bool isLeft(const glm::vec2& p) const {
        return !isRight(p);
    }

    [[nodiscard]] bool isRight(const glm::vec2& p) const {
        glm::vec2 SE = end.position - start.position;
        glm::vec2 SP = p - start.position;
        float cross = SE.x * SP.y - SE.y * SP.x;

        return cross <= 0;
    }

    [[nodiscard]] Vertex2D middle() const {
        glm::vec2 middlePos = (start.position + end.position) / 2.0f;
        glm::uvec2 middleCoords = (start.coordinates + end.coordinates) / 2u;

        return {middlePos, middleCoords};
    }
};

struct Triangle2D {
    Vertex2D v0, v1, v2;

    //Returns true of the winding order of this triangle is counterclockwise-wise. Returns false if it is clockwise.
    [[nodiscard]] bool isCCW() const {
        glm::vec2 a = v1.position - v0.position;
        glm::vec2 b = v2.position - v0.position;
        float cross = a.x * b.y - a.y * b.x;

        return cross > 0.0f;
    }
};
