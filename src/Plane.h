#pragma once
#include <glm/glm.hpp>

namespace TBNPlane {
    struct Plane {
        glm::vec3 T, B, N; //tangent, bi-tangent, and normal
        glm::vec3 origin;

        //Projects p to this plane
        //Returns a vec3 with xy being the plane coordinates, and z the value to displace up (along the plane normal)
        [[nodiscard]] glm::vec3 projectOnto(const glm::vec3& p) const {
            glm::vec3 movedP = p - origin;

            return {glm::vec2(dot(movedP, T), dot(movedP, B)), dot(movedP, N)};
        }
    };
}