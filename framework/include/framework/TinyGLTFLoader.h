#pragma once
#include "mesh.h"
#include <tinygltf/tiny_gltf.h>
#include "mesh_io_gltf.h"

class TinyGLTFLoader {
    tinygltf::Model model;
    std::vector<int> parent; //Parent of each bone. The value is the index of the bone parent

    template <typename T>
    std::vector<T> getAttributeData(const tinygltf::Primitive& primitive, const std::string& attributeName) {
        return getBufferData<T>(primitive.attributes.at(attributeName));
    }

    template <typename T>
    std::vector<T> getBufferData(const int index) const {
        const tinygltf::Accessor& accessor = model.accessors[index];
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

    void boneTransformations(Mesh& mesh) const;

public:
    explicit TinyGLTFLoader(const std::filesystem::path& file);

    std::vector<Mesh> toMesh(GLTFReadInfo& umeshReadInfo);
};
