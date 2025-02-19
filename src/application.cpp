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
        mesh = GPUMesh::loadGLTFMeshGPU(RESOURCE_ROOT "resources/cubeanim.gltf");

        try {
            m_defaultShader = ShaderBuilder().addVS(RESOURCE_ROOT "shaders/shader_vert.glsl").addFS(RESOURCE_ROOT "shaders/shader_frag.glsl").build();
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
                for(Animation& anim : m.cpuMesh.animation) {
                    auto transformationMatrix = anim.transformationMatrix(glfwGetTime());

                    m_defaultShader.bind();
                    glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvpMatrix));
                    //Uncomment this line when you use the modelMatrix (or fragmentPosition)
                    //glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
                    glUniformMatrix3fv(2, 1, GL_FALSE, glm::value_ptr(normalModelMatrix));
                    glUniform1i(4, GL_FALSE);
                    glUniform1i(5, m_useMaterial);
                    glUniformMatrix4fv(6, 1, GL_FALSE, glm::value_ptr(transformationMatrix));
                    m.draw(m_defaultShader);
                }
            }

            m_window.swapBuffers();
        }
    }

private:
    Window m_window;

    Shader m_defaultShader;

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
