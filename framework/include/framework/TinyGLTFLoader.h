#pragma once
#include "mesh.h"
#include <tinygltf/tiny_gltf.h>

class TinyGLTFLoader {
    tinygltf::Model model;

    template <typename T>
    std::vector<T> getAttributeData(const tinygltf::Primitive& primitive, const std::string& attributeName) {
        const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at(attributeName)];
        const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

        const auto* data = reinterpret_cast<const T*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

        return std::vector<T>(data, data + accessor.count);
    }

    /**
     * Transforms the vertices so that they are correctly placed in the scene. This is necessary because GLTF only stores
     * raw vertex data at the center of the scene (no translations, no scale, no rotation).
     */
    static void setupMeshesInScene(std::vector<Vertex>& vertices, const tinygltf::Node& node);

    void printGLTFBoneTransformations(Mesh& mesh) const;

public:
    explicit TinyGLTFLoader(const std::filesystem::path& file);

    std::vector<Mesh> toMesh();
};
