//#include "Image.h"
#include "mesh.h"
#include "texture.h"
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
#include <glm/mat4x4.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()
#include <framework/shader.h>
#include <framework/window.h>
#include <functional>
#include <iostream>
#include <vector>
#include <framework/trackball.h>

class Application {
public:
    Application(): m_window("Micro Meshes", glm::ivec2(1024, 1024), OpenGLVersion::GL45), m_texture(RESOURCE_ROOT "resources/checkerboard.png") {
        mesh = GPUMesh::loadGLTFMeshGPU(RESOURCE_ROOT "resources/cilinder.gltf");

        try {
            m_defaultShader = ShaderBuilder().addVS(RESOURCE_ROOT "shaders/shader_vert.glsl").addFS(RESOURCE_ROOT "shaders/shader_frag.glsl").build();
            skinningShader = ShaderBuilder().addVS(RESOURCE_ROOT "shaders/skinning.vert").addFS(RESOURCE_ROOT "shaders/skinning.frag").build();
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
            // Normals should be transformed differently than positions (ignoring translations + dealing with scaling):
            // https://paroj.github.io/gltut/Illumination/Tut09%20Normal%20Transformation.html
            const glm::mat3 normalModelMatrix = glm::inverseTranspose(glm::mat3(m_modelMatrix));

            for(GPUMesh& m : mesh) {
                skinningShader.bind();
                glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvpMatrix));

                auto bTs = m.cpuMesh.boneTransformations(glfwGetTime());
                m.draw(bTs);
            }

            m_window.swapBuffers();
        }
    }

private:
    Window m_window;

    Shader m_defaultShader;
    Shader skinningShader;

    std::vector<GPUMesh> mesh;

    Texture m_texture;
    bool m_useMaterial { false };

    std::unique_ptr<Trackball> trackball = std::make_unique<Trackball>(&m_window, glm::radians(50.0f));

    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 30.0f);
    glm::mat4 m_modelMatrix { 1.0f };

    void menu() {
        ImGui::Begin("Window");
        ImGui::Checkbox("Use material if no texture", &m_useMaterial);
        ImGui::End();
    }
};

int main() {
    Application app;
    app.update();

    return 0;
}
