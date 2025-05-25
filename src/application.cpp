#include "GPUMesh.h"
#include <framework/disable_all_warnings.h>
#include "framework/TinyGLTFLoader.h"
#include <windows.h>

#include "UploadBuffer.h"
DISABLE_WARNINGS_PUSH()
#include <glm/glm.hpp>
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
    Application(const std::filesystem::path& umeshPath, const std::filesystem::path& umeshAnimPath): window("Micro Meshes", glm::ivec2(1024, 1024), &gpuState) {
        if(!gpuState.createDevice(window.getHWND())) {
            gpuState.cleanupDevice();
            UnregisterClassW(window.getWc().lpszClassName, window.getWc().hInstance);
            exit(1);
        }
        gpuState.initImGui();
        gpuState.createDepthBuffer();

        mesh = GPUMesh::loadGLTFMeshGPU(umeshAnimPath, umeshPath, gpuState.get_device());

        try {
            skinningShader = ShaderBuilder().addVS(RESOURCE_ROOT L"shaders/skinningVS.hlsl").addPS(RESOURCE_ROOT L"shaders/skinningPS.hlsl").addConstantBuffers(5).build();
            //edgesShader = ShaderBuilder().addVS(RESOURCE_ROOT L"shaders/mesh_edges.vert").addPS(RESOURCE_ROOT L"shaders/mesh_edges.frag").build();
        } catch (ShaderLoadingException& e) {
            std::cerr << e.what() << std::endl;
        }

        gpuState.createPipeline(skinningShader);

        boneBuffer = UploadBuffer<glm::mat4>(gpuState.get_device(), 10, true);
        mvpBuffer = UploadBuffer<glm::mat4>(gpuState.get_device(), 1, true);
        mvBuffer = UploadBuffer<glm::mat4>(gpuState.get_device(), 1, true);
        displacementBuffer = UploadBuffer<float>(gpuState.get_device(), 1, true);
        cameraPosBuffer = UploadBuffer<glm::vec3>(gpuState.get_device(), 1, true);

    }

    void render() {
        const glm::mat4 mvMatrix = trackball->viewMatrix() * modelMatrix;
        const glm::mat4 mvpMatrix = projectionMatrix * mvMatrix;

        const auto bTs = mesh[0].cpuMesh.boneTransformations(gui.animation.time); //bone transformations

        boneBuffer.upload(bTs);
        gpuState.setConstantBuffer(0,  boneBuffer.getBuffer());

        mvpBuffer.upload({glm::transpose(mvpMatrix)});
        gpuState.setConstantBuffer(1,  mvpBuffer.getBuffer());

        mvBuffer.upload({glm::transpose(mvMatrix)});
        gpuState.setConstantBuffer(2,  mvBuffer.getBuffer());

        displacementBuffer.upload({gui.displace});
        gpuState.setConstantBuffer(3, displacementBuffer.getBuffer());

        cameraPosBuffer.upload({trackball->position()});
        gpuState.setConstantBuffer(4, cameraPosBuffer.getBuffer());

        gpuState.drawMesh(mesh[0].getVertexBufferView(), mesh[0].getIndexBufferView(), mesh[0].getIndexCount());
    }

    void update() {
        while (window.shouldClose()) {
            window.updateInput();
            Window::prepareFrame();

            if(!gui.animation.pause) gui.animation.time = std::fmod(getTime(), mesh[0].cpuMesh.animationDuration());

            menu();
            gpuState.renderFrame(ImVec4(0.29f, 0.29f, 0.29f, 1.00f), [this] { render(); }, window.windowSize);
        }
    }

    ~Application() {
        gpuState.waitForLastSubmittedFrame(); //Wait for the GPU to finish using up all resources before destroying everything
    }

private:
    GPUState gpuState;
    Window window;

    Shader skinningShader;
    Shader edgesShader;

    std::vector<GPUMesh> mesh;

    std::unique_ptr<Trackball> trackball = std::make_unique<Trackball>(&window, glm::radians(50.0f));

    glm::mat4 projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 1000.0f);
    glm::mat4 modelMatrix { 1.0f };

    struct {
        bool wireframe = false;
        float displace = 0.0f;

        struct {
            float time = 0.0f;
            bool pause = false;
        } animation;
    } gui;

    UploadBuffer<glm::mat4> boneBuffer;
    UploadBuffer<glm::mat4> mvpBuffer;
    UploadBuffer<glm::mat4> mvBuffer;
    UploadBuffer<float> displacementBuffer;
    UploadBuffer<glm::vec3> cameraPosBuffer;

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

    /**
     * @return time in seconds since application launched (actually, since the first time we call this function, but we
     * almost immediately call this function at startup)
     */
    static double getTime() {
        static LARGE_INTEGER frequency;
        static LARGE_INTEGER start;
        static bool initialized = false;

        if (!initialized) {
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&start);
            initialized = true;
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        return static_cast<double>(now.QuadPart - start.QuadPart) / frequency.QuadPart;
    }
};

int main(const int argc, char* argv[]) {
    if(argc == 1) {
        std::cerr << "Did not specify micro mesh file as program argument";
        return 1;
    }
    if(argc == 3) { //User specified micro mesh path and animation path
        const std::filesystem::path umeshPath(argv[1]);
        const std::filesystem::path umeshAnimPath(argv[2]);

        Application app(umeshPath, umeshAnimPath);
        app.update();
    } else {
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
    }

    return 0;
}
