#include "TinyGLTFLoader.h"

#include <iostream>
#include <ranges>
#include <glm/gtc/type_ptr.hpp>

TinyGLTFLoader::TinyGLTFLoader(const std::filesystem::path& file) {
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    if(!loader.LoadASCIIFromFile(&model, &err, &warn, file.string())) {
        throw std::runtime_error("Failed to load GLTF: " + err);
    }

    if(!warn.empty()) std::cerr << "GLTF Warning: " << warn << std::endl;

    parent.resize(model.skins[0].joints.size(), -1);

    auto joints = model.skins[0].joints;
    for(int i = 0; i < joints.size(); i++) {
        auto joint = joints[i];

        for(const auto child : model.nodes[joint].children) {
            auto it = std::ranges::find(joints, child);
            int childIndex = std::distance(joints.begin(), it);

            parent[childIndex] = i;
        }
    }
}

std::vector<Mesh> TinyGLTFLoader::toMesh() {
    std::vector<Mesh> out;

    for(int i = 0; i < model.meshes.size(); i++) {
        const auto& gltfMesh = model.meshes[i];
        Mesh myMesh;

        for(const tinygltf::Primitive& primitive : gltfMesh.primitives) {
            std::vector<Vertex> vertices;
            std::vector<glm::uvec3> triangles;

            // Extract vertex positions
            if(primitive.attributes.contains("POSITION")) {
                const auto positions = getAttributeData<glm::vec3>(primitive, "POSITION");

                vertices.resize(positions.size());
                for(size_t j = 0; j < positions.size(); j++) {
                    vertices[j].position = positions[j];
                }
            }

            // Extract vertex normals
            if(primitive.attributes.contains("NORMAL")) {
                const auto normals = getAttributeData<glm::vec3>(primitive, "NORMAL");

                for(size_t j = 0; j < normals.size(); j++) {
                    vertices[j].normal = normals[j];
                }
            }

            // Extract texture coordinates
            if(primitive.attributes.contains("TEXCOORD_0")) {
                const auto texCoords = getAttributeData<glm::vec2>(primitive, "TEXCOORD_0");

                for(size_t j = 0; j < texCoords.size(); j++) {
                    vertices[j].texCoord = texCoords[j];
                }
            }

            // Extract per-vertex bone indices
            if(primitive.attributes.contains("JOINTS_0")) {
                const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("JOINTS_0")];
                const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

                if(accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    const auto* joints = reinterpret_cast<const glm::u8vec4*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

                    for(size_t j = 0; j < accessor.count; j++) {
                        vertices[j].boneIndices = glm::ivec4(joints[j]);
                    }
                } else if(accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const auto* joints = reinterpret_cast<const glm::u16vec4*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);

                    for(size_t j = 0; j < accessor.count; j++) {
                        vertices[j].boneIndices = glm::ivec4(joints[j]);
                    }
                }
            }

            // Extract per-vertex bone weights
            if(primitive.attributes.contains("WEIGHTS_0")) {
                const auto weights = getAttributeData<glm::vec4>(primitive, "WEIGHTS_0");

                for(size_t j = 0; j < weights.size(); j++) {
                    vertices[j].boneWeights = weights[j];
                }
            }

            // Extract vertex indices per triangle
            const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
            const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
            const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

            if(indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const auto* indices = reinterpret_cast<const uint16_t*>(&indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
                for(size_t j = 0; j < indexAccessor.count; j += 3) {
                    triangles.emplace_back(indices[j], indices[j + 1], indices[j + 2]);
                }
            } else if(indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                const auto* indices = reinterpret_cast<const uint32_t*>(&indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
                for(size_t j = 0; j < indexAccessor.count; j += 3) {
                    triangles.emplace_back(indices[j], indices[j + 1], indices[j + 2]);
                }
            }

            myMesh.vertices = std::move(vertices);
            myMesh.triangles = std::move(triangles);
        }

        auto node = *std::ranges::find(model.nodes, i, &tinygltf::Node::mesh); //Find node corresponding to this mesh
        setupMeshesInScene(myMesh.vertices, node);

        boneTransformations(myMesh);

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

void TinyGLTFLoader::boneTransformations(Mesh& mesh) const {
    for(const auto& skin : model.skins) {
        for(const auto nodeID : skin.joints) {
            Bone b;

            for(const auto& animation : model.animations) {
                for(const auto& channel : animation.channels) {
                    if(channel.target_node == nodeID) {
                        const auto& sampler = animation.samplers[channel.sampler];
                        const auto& timeData = getBufferData<float>(sampler.input);

                        if(channel.target_path == "translation") {
                            b.translation.addTransformations(timeData, getBufferData<glm::vec3>(sampler.output));
                            b.translation.setInterpolationMode(sampler.interpolation);
                        }
                        else if(channel.target_path == "scale") {
                            b.scale.addTransformations(timeData, getBufferData<glm::vec3>(sampler.output));
                            b.scale.setInterpolationMode(sampler.interpolation);
                        }
                        else if(channel.target_path == "rotation") { //Rotation needs a bit more work, because we manually need to convert to quaternions
                            const auto& rotationData = getBufferData<glm::vec4>(sampler.output);

                            std::vector<glm::quat> quatRotations(rotationData.size());
                            std::transform(rotationData.begin(), rotationData.end(), quatRotations.begin(), [](glm::vec4 v) {
                                return glm::quat(v[3], v[0], v[1], v[2]);
                            });

                            b.rotation.addTransformations(timeData, quatRotations);
                            b.rotation.setInterpolationMode(sampler.interpolation);
                        }
                    }
                }
            }

            mesh.bones.push_back(b);
        }
    }
}

std::vector<glm::mat4> TinyGLTFLoader::getInverseBindMatrices() const {
    return getBufferData<glm::mat4>(model.skins[0].inverseBindMatrices);
}

