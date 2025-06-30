#include "mesh.h"

#include <ranges>
#include <unordered_map>
#include <queue>
#include <unordered_set>

struct VertexHash {
    size_t operator()(const Vertex& v) const {
        size_t seed = 0;
        hash_combine(seed, v.position.x);
        hash_combine(seed, v.position.y);
        hash_combine(seed, v.position.z);
        hash_combine(seed, v.normal.x);
        hash_combine(seed, v.normal.y);
        hash_combine(seed, v.normal.z);

        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneIndices0[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneIndices1[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneIndices2[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneWeights0[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneWeights1[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneWeights2[j]);

        hash_combine(seed, v.baryCoords.x);
        hash_combine(seed, v.baryCoords.y);
        hash_combine(seed, v.baryCoords.z);

        return seed;
    }

private:
    template <class T>
    static void hash_combine(std::size_t& seed, const T& v) {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
};

std::vector<glm::mat4> Mesh::boneTransformations(const float animTime) {
    std::vector<glm::mat4> transformations = {};

    for(int i = 0; i < bones.size(); i++) {
        auto globalT = globalTransform(animTime, i);

        transformations.push_back(glm::transpose(globalT)); //For some reason we need to take the transpose
    }

    return transformations;
}

glm::mat4 Mesh::globalTransform(const float animTime, const int index) {
    if(parent[index] == -1) return bones[index].transformationMatrix(animTime) * bones[index].ibm;

    return globalTransform(animTime, parent[index]) * bones[index].transformationMatrix(animTime) * bones[index].ibm;
}

float Mesh::animationDuration() const {
    return bones[0].translation.animationDuration();
}

std::vector<glm::uvec3> Mesh::baseTriangleIndices() const {
    const auto mapped = triangles | std::ranges::views::transform([](const Triangle& t) { return t.baseVertexIndices; });

    return {mapped.begin(), mapped.end()};
}

std::vector<glm::vec3> Triangle::baryCoords(const glm::vec3& A, const glm::vec3& B, const glm::vec3& C) const {
    std::vector<glm::vec3> baryCoords;
    baryCoords.reserve(uVertices.size());

    for(const auto& uv : uVertices) {
        baryCoords.push_back(computeBaryCoords(A, B, C, uv.position));
    }

    return baryCoords;
}

glm::vec3 Triangle::computeBaryCoords(const glm::vec3& A, const glm::vec3& B, const glm::vec3& C, const glm::vec3& pos) {
    const glm::vec3 v0 = B - A, v1 = C - A, v2 = pos - A;

    const float d00 = glm::dot(v0, v0);
    const float d01 = glm::dot(v0, v1);
    const float d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0);
    const float d21 = glm::dot(v2, v1);

    const float denom = d00 * d11 - d01 * d01;
    const float beta = (d11 * d20 - d01 * d21) / denom;
    const float gamma = (d00 * d21 - d01 * d20) / denom;
    const float alpha = 1.0f - beta - gamma;

    return {alpha, beta, gamma};
}

glm::mat4 Bone::transformationMatrix(const float animTime) {
    const auto t1 = translation.getTransformation(animTime);
    const auto t2 = rotation.getTransformation(animTime);
    const auto t3 = scale.getTransformation(animTime);

    return glm::translate(glm::mat4(1.0f), t1) * glm::mat4_cast(t2) * glm::scale(glm::mat4(1.0f), t3);
}

std::pair<std::vector<Vertex>, std::vector<glm::uvec3>> Mesh::allTriangles() const {
    std::unordered_map<Vertex, uint32_t, VertexHash> vertexCache;

    std::vector<Vertex> vs;
    std::vector<glm::uvec3> is;

    for(const auto& t : triangles) {
        //Base vertices
        const auto& bv0 = vertices[t.baseVertexIndices[0]];
        const auto& bv1 = vertices[t.baseVertexIndices[1]];
        const auto& bv2 = vertices[t.baseVertexIndices[2]];

        for(const auto& f : t.uFaces) {
            glm::uvec3 triangle;

            for(int i = 0; i < 3; i++) {
                const auto& uv = t.uVertices[f[i]];

                const auto bc = t.computeBaryCoords(bv0.position, bv1.position, bv2.position, uv.position);

                Vertex v {
                    .position = uv.position,
                    .normal = bc.x * bv0.normal + bc.y * bv1.normal + bc.z * bv2.normal, //interpolated normal
                    .direction = uv.displacement,
                    .baseBoneIndices0 = bv0.boneIndices,
                    .baseBoneIndices1 = bv1.boneIndices,
                    .baseBoneIndices2 = bv2.boneIndices,
                    .baseBoneWeights0 = bv0.boneWeights,
                    .baseBoneWeights1 = bv1.boneWeights,
                    .baseBoneWeights2 = bv2.boneWeights,
                    .baryCoords = bc
                };

                if(auto iter = vertexCache.find(v); iter != vertexCache.end()) {
                    // Already visited this vertex? Reuse it!
                    triangle[i] = iter->second;
                } else {
                    // New vertex? Create it and store it in the vertex cache.
                    vertexCache[v] = triangle[i] = vs.size();
                    vs.push_back(v);
                }
            }

            is.push_back(triangle);
        }
    }

    return {vs, is};
}

int Mesh::numberOfVerticesOnEdge() const {
    Triangle triangle = triangles[0];

    const auto v0 = vertices[triangle.baseVertexIndices.x];
    const auto v1 = vertices[triangle.baseVertexIndices.y];
    const auto v2 = vertices[triangle.baseVertexIndices.z];

    int vertexCount = 0;
    for(int i = triangle.uVertices.size() - 1; i >= 0; i--) {
        vertexCount++;

        const auto uv = triangle.uVertices[i];
        auto bc = Triangle::computeBaryCoords(v0.position, v1.position, v2.position, uv.position);

        if(bc == glm::vec3(0,1,0)) return vertexCount;
    }

    return 2; //In case there are no micro vertices, we just have 2 vertices on an edge
}

int Mesh::subdivisionLevel() const {
    return std::countr_zero(triangles[0].uFaces.size()) / 2;
}

std::vector<glm::vec2> Mesh::minMaxDisplacements(std::vector<int>& offsets) const {
    std::vector<glm::vec2> minMaxDisplacements;

    struct TriangleElement {
        std::vector<glm::uvec3> uTriangles; //Each element is a micro triangle that is defined by 3 indices into the micro vertex array
        glm::vec3 v0, v1, v2; //Corner vertices
    };

    for(const auto& t : triangles) {
        offsets.push_back(minMaxDisplacements.size());

        //First we compute the normal of the triangle's plane
        const auto v0 = vertices[t.baseVertexIndices.x];
        const auto v1 = vertices[t.baseVertexIndices.y];
        const auto v2 = vertices[t.baseVertexIndices.z];

        glm::vec3 e1 = v1.position - v0.position;
        glm::vec3 e2 = v2.position - v0.position;
        glm::vec3 N = glm::normalize(cross(e1, e2)); // plane normal

        //Then we set up the queue
        std::queue<TriangleElement> queue;
        queue.emplace(t.uFaces, v0.position, v1.position, v2.position);

        while(!queue.empty()) {
            //Compute min and max displacement of this triangle
            const auto currentTriangle = queue.front();
            queue.pop();
            float minDisplacement = 100000.0f, maxDisplacement = -100000.0f;

            for(const auto& ut : currentTriangle.uTriangles) {
                for(int i = 0; i < 3; i++) {
                    float height = glm::dot(t.uVertices[ut[i]].displacement, N);

                    maxDisplacement = std::max(maxDisplacement, height);
                    minDisplacement = std::min(minDisplacement, height);
                }
            }

            minMaxDisplacements.emplace_back(minDisplacement, maxDisplacement);

            if(currentTriangle.uTriangles.size() != 1) {
                //Now we compute the next 4 triangles for processing
                glm::vec3 v0v1 = (currentTriangle.v0 + currentTriangle.v1) / 2.0f;
                glm::vec3 v0v2 = (currentTriangle.v0 + currentTriangle.v2) / 2.0f;
                glm::vec3 v1v2 = (currentTriangle.v1 + currentTriangle.v2) / 2.0f;
                TriangleElement t1{.v0 = currentTriangle.v0, .v1 = v0v1, .v2 = v0v2}; //Triangle near v0
                TriangleElement t2{.v0 = v0v1, .v1 = currentTriangle.v1, .v2 = v1v2}; //Triangle near v1
                TriangleElement t3{.v0 = v0v1, .v1 = v1v2, .v2 = v0v2}; //Center triangle
                TriangleElement t4{.v0 = v0v2, .v1 = v1v2, .v2 = currentTriangle.v2}; //Triangle near v2

                for(const auto& ut : currentTriangle.uTriangles) {
                    glm::vec3 midPoint = (1.0f/3.0f) * t.uVertices[ut[0]].position + (1.0f/3.0f) * t.uVertices[ut[1]].position + (1.0f/3.0f) * t.uVertices[ut[2]].position;
                    glm::vec3 bc = Triangle::computeBaryCoords(currentTriangle.v0, currentTriangle.v1, currentTriangle.v2, midPoint);

                    if(bc.x > 0.5) t1.uTriangles.push_back(ut);
                    else if(bc.y > 0.5) t2.uTriangles.push_back(ut);
                    else if(bc.z > 0.5) t4.uTriangles.push_back(ut);
                    else t3.uTriangles.push_back(ut);
                }

                queue.emplace(t1);
                queue.emplace(t2);
                queue.emplace(t3);
                queue.emplace(t4);
            }
        }
    }

    return minMaxDisplacements;
}

glm::vec3 getPlanePosition(glm::vec2 coords, int dOffset, const std::vector<glm::vec3>& positions2D) {
    int sum = (coords.x * (coords.x + 1)) / 2; //Sum from 1 until coords.x (closed formula of summation)
    int index = sum + coords.y;

    return positions2D[dOffset + index];
}

float distPointToEdge(const glm::vec2& p, const Edge2D& e) {
    const glm::vec2 a = e.start.position;
    const glm::vec2 b = e.end.position;

    const glm::vec2 ab = b - a;
    const glm::vec2 ap = p - a;

    float abLengthSquared = glm::dot(ab, ab);

    float t = glm::dot(ap, ab) / abLengthSquared;
    t = std::clamp(t, 0.0f, 1.0f);

    glm::vec2 closest = a + t * ab;
    return glm::length(p - closest);
}

struct Line {
    double a, b; //Linear line y = ax + b

    //Returns the intersection point of this line with another line
    [[nodiscard]] glm::vec2 intersect(const Line& other) const {
        double x = (other.b - b) / (a - other.a);
        double y = a * x + b;
        return {x, y};
    }
};

//Computes a line through p0 and p1, and then translates the line to go through p2 (i.e., same slope as the line from p0 to p1)
Line computeTranslatedLine(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2) {
    double dx = p1.x - p0.x;
    double dy = p1.y - p0.y;
    double a = dy / dx;

    double b = p2.y - a * p2.x;

    return {a, b};
}

Triangle2DOnlyPos bindVerticesForTriangle(const Triangle2D& t, int dOffset, const std::vector<glm::vec3>& positions2D) {
    const auto v0 = t.v0;
    const auto v1 = t.v1;
    const auto v2 = t.v2;

    std::vector<Edge2D> edges{ {v0, v1}, {v1, v2}, {v2, v0} };
    std::vector vertices{v0, v1, v2};
    for(int i = 0; i < 3; i++) {
        const auto& e = edges[i];
        int incrementX = 0, incrementY = 0;
        float maxDistance = 0.0f;

        glm::uvec2 startCoord = min(e.start.coordinates, e.end.coordinates);
        glm::uvec2 endCoord = max(e.start.coordinates, e.end.coordinates);

        if(startCoord.x == endCoord.x) incrementY = 1;
        else if(startCoord.y == endCoord.y) incrementX = 1;
        else {
            incrementX = 1;
            incrementY = 1;
        }

        startCoord = {startCoord.x + incrementX, startCoord.y + incrementY};

        while(startCoord != endCoord) {
            const auto temp = getPlanePosition(startCoord, dOffset, positions2D);
            Vertex2D planePos = {{temp.x, temp.y}, startCoord};
            float dist = distPointToEdge(planePos.position, e);

            if(e.isRight(planePos.position) && dist > maxDistance) {
                maxDistance = dist;
                vertices[i] = planePos;
            }

            startCoord = {startCoord.x + incrementX, startCoord.y + incrementY};
        }
    }

    const auto lv0v1 = computeTranslatedLine(v0.position, v1.position, vertices[0].position);
    const auto lv1v2 = computeTranslatedLine(v1.position, v2.position, vertices[1].position);
    const auto lv0v2 = computeTranslatedLine(v0.position, v2.position, vertices[2].position);

    return{lv0v1.intersect(lv0v2), lv0v1.intersect(lv1v2), lv0v2.intersect(lv1v2)};
}

std::vector<Triangle2DOnlyPos> Mesh::boundingVertices(const std::vector<glm::vec3>& positions2D, const std::vector<int>& dOffsets) const {
    std::vector<Triangle2DOnlyPos> boundTriangles;

    struct TriangleElement {
        std::vector<glm::uvec3> uTriangles; //Each element is a micro triangle that is defined by 3 indices into the micro vertex array
        glm::vec3 v0, v1, v2; //Corner vertices, not displaced
        Triangle2D t2D;
    };

    int counter = 0;
    for(const auto& [t, dOffset] : std::views::zip(triangles, dOffsets)) {
        const auto nRows = numberOfVerticesOnEdge();

        const auto v0 = vertices[t.baseVertexIndices.x];
        const auto v1 = vertices[t.baseVertexIndices.y];
        const auto v2 = vertices[t.baseVertexIndices.z];

        const Vertex2D v02D(getPlanePosition(glm::vec2{0,0}, dOffset, positions2D), {0, 0});
        const Vertex2D v12D(getPlanePosition(glm::vec2{nRows - 1,0}, dOffset, positions2D), {nRows - 1,0});
        const Vertex2D v22D(getPlanePosition(glm::vec2{nRows - 1,nRows - 1}, dOffset, positions2D), {nRows - 1,nRows - 1});
        Triangle2D tr2D(v02D, v12D, v22D);

        //Set up the queue
        std::queue<TriangleElement> queue;
        queue.emplace(t.uFaces, v0.position, v1.position, v2.position, tr2D);

        while(!queue.empty()) {
            const auto currentTriangle = queue.front();
            queue.pop();

            //Compute bound triangle
            const auto boundVs = bindVerticesForTriangle(currentTriangle.t2D, dOffset, positions2D);
            boundTriangles.emplace_back(boundVs.v0, boundVs.v1, boundVs.v2);

            //Add next elements to queue
            if(currentTriangle.uTriangles.size() != 1) {
                //Now we compute the next 4 triangles for processing
                glm::vec3 v0v1 = (currentTriangle.v0 + currentTriangle.v1) / 2.0f;
                glm::vec3 v0v2 = (currentTriangle.v0 + currentTriangle.v2) / 2.0f;
                glm::vec3 v1v2 = (currentTriangle.v1 + currentTriangle.v2) / 2.0f;

                Edge2D e12D(currentTriangle.t2D.v0, currentTriangle.t2D.v1);
                Edge2D e22D(currentTriangle.t2D.v1, currentTriangle.t2D.v2);
                Edge2D e32D(currentTriangle.t2D.v2, currentTriangle.t2D.v0);

                const auto e1Middle = e12D.middle();
                const auto e2Middle = e22D.middle();
                const auto e3Middle = e32D.middle();

                Vertex2D v0v12D(getPlanePosition(e1Middle.coordinates, dOffset, positions2D), e1Middle.coordinates);
                Vertex2D v1v22D(getPlanePosition(e2Middle.coordinates, dOffset, positions2D), e2Middle.coordinates);
                Vertex2D v2v02D(getPlanePosition(e3Middle.coordinates, dOffset, positions2D), e3Middle.coordinates);

                TriangleElement t1{.v0 = currentTriangle.v0, .v1 = v0v1, .v2 = v0v2, .t2D = {currentTriangle.t2D.v0, v0v12D, v2v02D}}; //Triangle near v0
                TriangleElement t2{.v0 = v0v1, .v1 = currentTriangle.v1, .v2 = v1v2, .t2D = {v0v12D, currentTriangle.t2D.v1, v1v22D}}; //Triangle near v1
                TriangleElement t3{.v0 = v0v1, .v1 = v1v2, .v2 = v0v2, .t2D = {v0v12D, v1v22D, v2v02D}}; //Center triangle
                TriangleElement t4{.v0 = v0v2, .v1 = v1v2, .v2 = currentTriangle.v2, .t2D = {v2v02D, v1v22D, currentTriangle.t2D.v2}}; //Triangle near v2

                for(const auto& ut : currentTriangle.uTriangles) {
                    glm::vec3 midPoint = (1.0f/3.0f) * t.uVertices[ut[0]].position + (1.0f/3.0f) * t.uVertices[ut[1]].position + (1.0f/3.0f) * t.uVertices[ut[2]].position;
                    glm::vec3 bc = Triangle::computeBaryCoords(currentTriangle.v0, currentTriangle.v1, currentTriangle.v2, midPoint);

                    if(bc.x > 0.5) t1.uTriangles.push_back(ut);
                    else if(bc.y > 0.5) t2.uTriangles.push_back(ut);
                    else if(bc.z > 0.5) t4.uTriangles.push_back(ut);
                    else t3.uTriangles.push_back(ut);
                }

                queue.emplace(t1);
                queue.emplace(t2);
                queue.emplace(t3);
                queue.emplace(t4);
            }
        }

        counter++;
    }

    return boundTriangles;
}