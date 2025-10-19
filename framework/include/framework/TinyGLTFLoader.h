#pragma once

#include "mesh.h"
#include <filesystem>
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <tinygltf/tiny_gltf.h>
#include "mesh_io_gltf.h"
DISABLE_WARNINGS_POP()

class TinyGLTFLoader {
    tinygltf::Model umeshModel;
    SubdivisionMesh umesh;

    template <typename T>
    std::vector<T> getAttributeData(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const std::string& attributeName) {
        return getBufferData<T>(model, primitive.attributes.at(attributeName));
    }

    template <typename T>
    std::vector<T> getBufferData(const tinygltf::Model& model, const int index) const {
        const tinygltf::Accessor& accessor = model.accessors[index];
        const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

        const auto* data = reinterpret_cast<const T*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

        return std::vector<T>(data, data + accessor.count);
    }

    /**
    * Fetch the vertex displacement direction given its position.
    */
    [[nodiscard]] glm::vec3 getVertexDisplacementDir(glm::vec3 position) const;

public:
    TinyGLTFLoader(const std::filesystem::path& umeshFilePath , GLTFReadInfo& umeshReadInfo);

    Mesh toMesh();
};
