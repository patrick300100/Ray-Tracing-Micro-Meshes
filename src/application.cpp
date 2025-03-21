#include "GPUMesh.h"
// Always include window first (because it includes glfw, which includes GL which needs to be included AFTER glew).
// Can't wait for modules to fix this stuff...
#include <framework/disable_all_warnings.h>

#include "framework/TinyGLTFLoader.h"
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
// Include glad before glfw3
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()
#include <framework/shader.h>
#include <framework/window.h>
#include <iostream>
#include <vector>
#include <framework/trackball.h>

class Application {
public:
    Application(const std::filesystem::path& umeshPath, const std::filesystem::path& umeshAnimPath): m_window("Micro Meshes", glm::ivec2(1024, 1024), OpenGLVersion::GL45) {
        mesh = GPUMesh::loadGLTFMeshGPU(umeshAnimPath, umeshPath);

        try {
            skinningShader = ShaderBuilder().addVS(RESOURCE_ROOT "shaders/skinning.vert").addFS(RESOURCE_ROOT "shaders/skinning.frag").build();
            edgesShader = ShaderBuilder().addVS(RESOURCE_ROOT "shaders/mesh_edges.vert").addFS(RESOURCE_ROOT "shaders/mesh_edges.frag").build();
        } catch (ShaderLoadingException& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    void update() {
        while (!m_window.shouldClose()) {
            m_window.updateInput();

            if(!gui.animation.pause) gui.animation.time = std::fmod(glfwGetTime(), mesh[0].cpuMesh.animationDuration());

            menu();

            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);

            const glm::mat4 mvMatrix = trackball->viewMatrix() * m_modelMatrix;
            const glm::mat4 mvpMatrix = m_projectionMatrix * mvMatrix;

            for(GPUMesh& m : mesh) {
                skinningShader.bind();
                glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvpMatrix));
                glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(mvMatrix));
                glUniform1f(2, gui.displace);
                glUniform3fv(3, 1, glm::value_ptr(trackball->position()));

                auto bTs = m.cpuMesh.boneTransformations(gui.animation.time); //bone transformations
                m.draw(bTs);

                if(gui.wireframe) {
                    edgesShader.bind();
                    m.drawWireframe(bTs, mvpMatrix, gui.displace);
                }
            }

            m_window.swapBuffers();
        }
    }

private:
    Window m_window;

    Shader skinningShader;
    Shader edgesShader;

    std::vector<GPUMesh> mesh;

    std::unique_ptr<Trackball> trackball = std::make_unique<Trackball>(&m_window, glm::radians(50.0f));

    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 30.0f);
    glm::mat4 m_modelMatrix { 1.0f };

    struct {
        bool wireframe = false;
        float displace = 0.0f;

        struct {
            float time = 0.0f;
            bool pause = false;
        } animation;
    } gui;

    void menu() {
        ImGui::Begin("Window");

        if(ImGui::BeginTabBar("MainTabs")) {
            if(ImGui::BeginTabItem("µMesh")) {
                ImGui::Checkbox("Wireframe", &gui.wireframe);
                ImGui::SliderFloat("Displace", &gui.displace, 0.0f, 1.0f);

                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Animation")) {
                ImGui::Checkbox("Pause", &gui.animation.pause);

                ImGui::BeginDisabled(!gui.animation.pause);
                ImGui::SliderFloat("Time", &gui.animation.time, 0.0f, mesh[0].cpuMesh.animationDuration() - 0.001f, "%.3f");
                ImGui::EndDisabled();

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }
};

int main(const int argc, char* argv[]) {
    if(argc == 1) {
        std::cerr << "Did not specify micro mesh file as program argument";
        return 1;
    }

    const std::filesystem::path umeshPath(argv[1]);

    const auto parentDir = umeshPath.parent_path();
    const auto filenameStem = umeshPath.stem().string();
    const auto extension = umeshPath.extension().string();

    //GLTF has 2 file extension: .gltf and .glb. We select which one is present.
    const auto umeshAnimPathGLTF = parentDir / (filenameStem + "_anim" + ".gltf");
    const auto umeshAnimPathGLB = parentDir / (filenameStem + "_anim" + ".glb");

    std::filesystem::path umeshAnimPath;
    if(exists(umeshAnimPathGLTF)) umeshAnimPath = umeshAnimPathGLTF;
    else if(exists(umeshAnimPathGLB)) umeshAnimPath = umeshAnimPathGLB;
    else {
        std::cerr << "Could not find animation data. Remember that it has to be in the same directory.";
        return 1;
    }

    Application app(umeshPath, umeshAnimPath);
    app.update();

    return 0;
}
