#include "TinyGLTFLoader.h"

#include <iostream>
#include <glm/gtc/type_ptr.hpp>

TinyGLTFLoader::TinyGLTFLoader(const std::filesystem::path& file) {
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    if(!loader.LoadASCIIFromFile(&model, &err, &warn, file.string())) {
        throw std::runtime_error("Failed to load GLTF: " + err);
    }

    if(!warn.empty()) std::cerr << "GLTF Warning: " << warn << std::endl;

    parent.resize(model.skins[0].joints.size(), -1);

    for(const auto nodeIndex : model.skins[0].joints) {
        for(const auto child : model.nodes[nodeIndex].children) {
            parent[child] = nodeIndex;
        }
    }
}

std::vector<Mesh> TinyGLTFLoader::toMesh() {
    std::vector<Mesh> out;

    for(const tinygltf::Mesh& gltfMesh : model.meshes) {
        Mesh myMesh;

        for(const tinygltf::Primitive& primitive : gltfMesh.primitives) {
            std::vector<Vertex> vertices;
            std::vector<glm::uvec3> triangles;

            // Extract vertex positions
            if(primitive.attributes.contains("POSITION")) {
                const auto positions = getAttributeData<glm::vec3>(primitive, "POSITION");

                vertices.resize(positions.size());
                for(size_t i = 0; i < positions.size(); i++) {
                    vertices[i].position = positions[i];
                }
            }

            // Extract vertex normals
            if(primitive.attributes.contains("NORMAL")) {
                const auto normals = getAttributeData<glm::vec3>(primitive, "NORMAL");

                for(size_t i = 0; i < normals.size(); i++) {
                    vertices[i].normal = normals[i];
                }
            }

            // Extract texture coordinates
            if(primitive.attributes.contains("TEXCOORD_0")) {
                const auto texCoords = getAttributeData<glm::vec2>(primitive, "TEXCOORD_0");

                for(size_t i = 0; i < texCoords.size(); i++) {
                    vertices[i].texCoord = texCoords[i];
                }
            }

            // Extract per-vertex bone indices
            if(primitive.attributes.contains("JOINTS_0")) {
                const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("JOINTS_0")];
                const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

                if(accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    const auto* joints = reinterpret_cast<const glm::u8vec4*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

                    for(size_t i = 0; i < accessor.count; i++) {
                        vertices[i].boneIndices = glm::ivec4(joints[i]);
                    }
                } else if(accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const auto* joints = reinterpret_cast<const glm::u16vec4*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

                    for(size_t i = 0; i < accessor.count; i++) {
                        vertices[i].boneIndices = glm::ivec4(joints[i]);
                    }
                }
            }

            // Extract per-vertex bone weights
            if(primitive.attributes.contains("WEIGHTS_0")) {
                const auto weights = getAttributeData<glm::vec4>(primitive, "WEIGHTS_0");

                for(size_t i = 0; i < weights.size(); i++) {
                    vertices[i].boneWeights = weights[i];
                }
            }

            // Extract vertex indices per triangle
            const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
            const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
            const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

            if(indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const auto* indices = reinterpret_cast<const uint16_t*>(&indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
                for(size_t i = 0; i < indexAccessor.count; i += 3) {
                    triangles.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
                }
            } else if(indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                const auto* indices = reinterpret_cast<const uint32_t*>(&indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
                for(size_t i = 0; i < indexAccessor.count; i += 3) {
                    triangles.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
                }
            }

            myMesh.vertices = std::move(vertices);
            myMesh.triangles = std::move(triangles);
        }

        auto nodeIt = std::ranges::find_if(model.nodes, [&gltfMesh](const tinygltf::Node& node) { return node.name == gltfMesh.name; });
        setupMeshesInScene(myMesh.vertices, *nodeIt);

        printGLTFBoneTransformations(myMesh);

        myMesh.parent = std::move(parent);
        myMesh.ibms = std::move(getInverseBindMatrices());

        out.push_back(myMesh);
    }

    return out;
}

void TinyGLTFLoader::setupMeshesInScene(std::vector<Vertex>& vertices, const tinygltf::Node& node) {
    glm::mat4 transformation;

    if(!node.matrix.empty()) transformation = glm::make_mat4(node.matrix.data()); //If there's a transformation matrix, use it directly
    else { //If no matrix, build the transformation matrix from the individual components
        glm::vec3 translation(0.0f);
        auto rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale(1.0f);

        if(node.translation.size() == 3) translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        if(node.rotation.size() == 4) rotation = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
        if(node.scale.size() == 3) scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);

        transformation = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
    }

    for(auto& vertex : vertices) {
        vertex.position = glm::vec3(transformation * glm::vec4(vertex.position, 1.0f));
        vertex.normal = glm::normalize(glm::vec3(transformation * glm::vec4(vertex.normal, 0.0f)));
    }
}

void TinyGLTFLoader::printGLTFBoneTransformations(Mesh& mesh) const {
    for (size_t animIndex = 0; animIndex < model.animations.size(); ++animIndex) {
        const auto& animation = model.animations[animIndex];
        std::cout << "Animation " << animIndex << ":\n";

        int boneIndex = -1;
        for(int i = 0; i < animation.channels.size(); i++) {
            const auto& channel = animation.channels[i];

            if(i % 3 == 0) {
                boneIndex++;
                mesh.animation.emplace_back();
                mesh.animation[boneIndex].name = model.nodes[channel.target_node].name;
            }

            int nodeIndex = channel.target_node;
            if (nodeIndex < 0 || nodeIndex >= model.nodes.size()) continue;

            const auto& sampler = animation.samplers[channel.sampler];

            // Access time keyframes
            const auto timeData = getBufferData<float>(sampler.input);

            int keyframeCount = timeData.size();

            mesh.animation[boneIndex].duration = std::max(mesh.animation[boneIndex].duration, timeData.back());

            // Access transformation keyframes
            const auto& transformAccessor = model.accessors[sampler.output];
            const auto& transformBufferView = model.bufferViews[transformAccessor.bufferView];
            const auto& transformBuffer = model.buffers[transformBufferView.buffer];

            // Print keyframe transformations
            std::cout << "  Node " << nodeIndex << " (" << model.nodes[nodeIndex].name << ") " << " (" << channel.target_path << "):\n";
            for (int k = 0; k < keyframeCount; ++k) {
                float time = timeData[k];
                glm::mat4 transform = glm::mat4(1.0f);

                if (channel.target_path == "translation") {
                    const glm::vec3* translationData = reinterpret_cast<const glm::vec3*>(&transformBuffer.data[transformBufferView.byteOffset + transformAccessor.byteOffset]);
                    transform = glm::translate(glm::mat4(1.0f), translationData[k]);
                    mesh.animation[boneIndex].translation.addTransformation(time, translationData[k]);
                    mesh.animation[boneIndex].translation.setInterpolationMode(sampler.interpolation);
                }
                else if (channel.target_path == "rotation") {
                    const glm::quat* rotationData = reinterpret_cast<const glm::quat*>(&transformBuffer.data[transformBufferView.byteOffset + transformAccessor.byteOffset]);
                    transform = glm::mat4_cast(rotationData[k]);
                    mesh.animation[boneIndex].rotation.addTransformation(time, rotationData[k]);
                    mesh.animation[boneIndex].rotation.setInterpolationMode(sampler.interpolation);
                }
                else if (channel.target_path == "scale") {
                    const glm::vec3* scaleData = reinterpret_cast<const glm::vec3*>(&transformBuffer.data[transformBufferView.byteOffset + transformAccessor.byteOffset]);
                    transform = glm::scale(glm::mat4(1.0f), scaleData[k]);
                    mesh.animation[boneIndex].scale.addTransformation(time, scaleData[k]);
                    mesh.animation[boneIndex].scale.setInterpolationMode(sampler.interpolation);
                }

                // Print matrix
                std::cout << "    Keyframe " << k << " (Time: " << time << "s):\n";
                for (int i = 0; i < 4; ++i) {
                    std::cout << "      ["
                              << transform[0][i] << ", " << transform[1][i] << ", "
                              << transform[2][i] << ", " << transform[3][i] << "]\n";
                }
            }
        }
    }
}

std::vector<glm::mat4> TinyGLTFLoader::getInverseBindMatrices() const {
    return getBufferData<glm::mat4>(model.skins[0].inverseBindMatrices);
}

