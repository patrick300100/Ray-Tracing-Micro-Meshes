#include "TinyGLTFLoader.h"

#include <framework/disable_all_warnings.h>
#include <iostream>
#include <ranges>
#include <unordered_set>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/type_ptr.hpp>
DISABLE_WARNINGS_POP()

TinyGLTFLoader::TinyGLTFLoader(const std::filesystem::path& umeshFilePath, GLTFReadInfo& umeshReadInfo) {
    std::string err, warn;
    tinygltf::TinyGLTF loader;

    if(umeshFilePath.extension().string() == ".gltf") {
        if(!loader.LoadASCIIFromFile(&umeshModel, &err, &warn, umeshFilePath.string())) throw std::runtime_error("Failed to load GLTF: " + err);
    } else {
        if(!loader.LoadBinaryFromFile(&umeshModel, &err, &warn, umeshFilePath.string())) throw std::runtime_error("Failed to load GLB: " + err);
    }

    if(!warn.empty()) std::cerr << "GLTF Warning: " << warn << std::endl;

    umesh = umeshReadInfo.get_subdivision_mesh();
}

Mesh TinyGLTFLoader::toMesh() {
    const auto umeshPrimitive = umeshModel.meshes[0].primitives[0];

    Mesh myMesh;
    std::vector<Vertex> vertices;

    // Extract vertex positions
    {
        const auto positions = getAttributeData<glm::vec3>(umeshModel, umeshPrimitive, "POSITION");

        vertices.resize(positions.size());
        for(size_t j = 0; j < positions.size(); j++) {
            vertices[j].position = positions[j];
        }
    }

    // Extract vertex normals
    {
        const auto normals = getAttributeData<glm::vec3>(umeshModel, umeshPrimitive, "NORMAL");

        for(size_t j = 0; j < normals.size(); j++) {
            vertices[j].normal = normals[j];
        }
    }

    myMesh.vertices = std::move(vertices);

    const std::vector<unsigned int> indicesFlat = getBufferData<unsigned int>(umeshModel, umeshModel.meshes[0].primitives[0].indices);
    std::vector<glm::uvec3> triangles; //Indices into vertex array
    for(int j = 0; j < indicesFlat.size(); j += 3) {
        triangles.emplace_back(indicesFlat[j], indicesFlat[j + 1], indicesFlat[j + 2]);
    }

    for(const auto& [f, t] : std::views::zip(umesh.faces, triangles)) {
        std::unordered_set<unsigned int> uvIndices;
        std::vector<glm::uvec3> ufs; //micro faces
        for(int j = 0; j < f.F.rows(); j++) {
            const auto& indices = f.F.row(j);

            ufs.emplace_back(indices(0), indices(1), indices(2));
            uvIndices.insert(indices.begin(), indices.end());
        }

        std::vector<uVertex> uvs; //micro vertices
        for(int j = 0; j < f.V.rows(); j++) {
            const auto& pos = f.V.row(j);
            const auto& dis = f.VD.row(j);

            uvs.emplace_back(
                glm::vec3{pos(0), pos(1), pos(2)},
                glm::vec3{dis(0), dis(1), dis(2)},
                uvIndices.contains(j)
            );
        }

        myMesh.triangles.emplace_back(t, uvs, ufs);
    }

    for(auto& v : myMesh.vertices) {
        v.direction = getVertexDisplacementDir(v.position);
    }

    return myMesh;
}

glm::vec3 TinyGLTFLoader::getVertexDisplacementDir(const glm::vec3 position) const {
    for(const auto& f : umesh.faces) {
        for(int i = 0; i < 3; i++) {
            auto pos = f.base_V.row(i);

            //Read: position == pos. But since floats can have precision errors, we use an epsilon check instead.
            if(abs(position.x - pos(0)) <= 0.001f && abs(position.y - pos(1)) <= 0.001f && abs(position.z - pos(2)) <= 0.001f) {
                const auto& displacement = f.base_VD.row(i);
                return {displacement(0), displacement(1), displacement(2)};
            }
        }
    }

    throw std::runtime_error("Vertex displacement not found");
}
