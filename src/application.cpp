//#include "Image.h"
#include "mesh.h"
// Always include window first (because it includes glfw, which includes GL which needs to be included AFTER glew).
// Can't wait for modules to fix this stuff...
#include <framework/disable_all_warnings.h>

#include "tangent.h"
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

            menu();

            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);

            const glm::mat4 mvpMatrix = m_projectionMatrix * trackball->viewMatrix() * m_modelMatrix;

            for(GPUMesh& m : mesh) {
                skinningShader.bind();
                glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvpMatrix));

                auto bTs = m.cpuMesh.boneTransformations(glfwGetTime()); //bone transformations
                m.draw(bTs);

                edgesShader.bind();
                m.drawWireframe(bTs, mvpMatrix);
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

    void menu() {
        ImGui::Begin("Window");
        ImGui::End();
    }
};

int main(const int argc, char* argv[]) {
    if(argc == 1) {
        std::cerr << "Did not specify micro mesh file in the command line.";
        return 1;
    }

    const std::filesystem::path umeshPath(argv[1]);

    const auto parentDir = umeshPath.parent_path();
    const auto filenameStem = umeshPath.stem().string();
    const auto extension = umeshPath.extension().string();

    //GLTF has 2 file extension: *.gltf and *.glb. We select which one is present.
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
