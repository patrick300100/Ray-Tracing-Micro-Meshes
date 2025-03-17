#include "TinyGLTFLoader.h"

#include <iostream>
#include <ranges>
#include <glm/gtc/type_ptr.hpp>

TinyGLTFLoader::TinyGLTFLoader(const std::filesystem::path& animFilePath, GLTFReadInfo& umeshReadInfo) {
    std::string err, warn;

    if(tinygltf::TinyGLTF loader; !loader.LoadASCIIFromFile(&model, &err, &warn, animFilePath.string())) {
        throw std::runtime_error("Failed to load GLTF: " + err);
    }

    if(!warn.empty()) std::cerr << "GLTF Warning: " << warn << std::endl;

    parent.resize(model.skins[0].joints.size(), -1);

    auto joints = model.skins[0].joints;
    for(int i = 0; i < joints.size(); i++) {
        for(const auto joint = joints[i]; const auto child : model.nodes[joint].children) {
            const auto it = std::ranges::find(joints, child);
            const auto childIndex = std::distance(joints.begin(), it);

            parent[childIndex] = i;
        }
    }

    umesh = umeshReadInfo.get_subdivision_mesh();
}

std::vector<Mesh> TinyGLTFLoader::toMesh(GLTFReadInfo& umeshReadInfo) {
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
                auto processJoints = [&]<typename T0>(T0 /*type*/) {
                    const auto joints = getAttributeData<T0>(primitive, "JOINTS_0");

                    for(int j = 0; j < joints.size(); j++) {
                        vertices[j].boneIndices = glm::ivec4(joints[j]);
                    }
                };

                switch(model.accessors[primitive.attributes.at("JOINTS_0")].componentType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: processJoints(glm::u8vec4{}); break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: processJoints(glm::u16vec4{}); break;
                    default: throw std::runtime_error("Component type cannot be processed for bone indices. Please add.");
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
            auto processIndices = [&]<typename T0>(T0 /*type*/) {
                const auto indices = getBufferData<T0>(primitive.indices);
                
                for(int j = 0; j < indices.size(); j += 3) {
                    triangles.emplace_back(indices[j], indices[j + 1], indices[j + 2]);
                }
            };

            switch(model.accessors[primitive.indices].componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: processIndices(uint16_t{}); break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: processIndices(uint32_t{}); break;
                default: throw std::runtime_error("Component type cannot be processed for vertex indices. Please add.");
            }

            myMesh.vertices = std::move(vertices);

            myMesh.baseTriangleIndices = triangles;

            for(const auto& triangle : triangles) {
                myMesh.triangles.emplace_back(triangle);
            }
        }

        auto node = *std::ranges::find(model.nodes, i, &tinygltf::Node::mesh); //Find node corresponding to this mesh
        setupMeshesInScene(myMesh.vertices, node);

        boneTransformations(myMesh);

        myMesh.parent = std::move(parent);

        myMesh.umesh = umeshReadInfo.get_subdivision_mesh();

        for(auto& v : myMesh.vertices) {
            v.displacement = getVertexDisplacement(v.position);
        }

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
    const auto& ibms = getBufferData<glm::mat4>(model.skins[0].inverseBindMatrices);

    for(const auto& skin : model.skins) {
        for(const auto& [nodeID, ibm] : std::views::zip(skin.joints, ibms)) {
            Bone b;
            b.ibm = ibm;

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

glm::vec3 TinyGLTFLoader::getVertexDisplacement(const glm::vec3 position) const {
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

