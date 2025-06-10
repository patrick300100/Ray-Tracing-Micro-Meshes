#include "GPUMesh.h"
#include <framework/disable_all_warnings.h>
#include "framework/TinyGLTFLoader.h"
#include <windows.h>

#include "CommandSender.h"
#include "RasterizationShader.h"
#include "RayTraceShader.h"
#include "UploadBuffer.h"
DISABLE_WARNINGS_PUSH()
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()
#include <shader.h>
#include <framework/window.h>
#include <iostream>
#include <vector>
#include <ranges>
#include <framework/trackball.h>

struct RayTraceVertex {
    glm::vec3 position;
};

struct AABB {
    glm::vec3 minPos;
    glm::vec3 maxPos;
    glm::uvec3 vIndices;
    int nRows; //Number of micro vertices on the base edge of the triangle
    int displacementOffset; //Offset into the displacement buffer from where displacements for this triangle starts
};

class Application {
public:
    Application(const std::filesystem::path& umeshPath, const std::filesystem::path& umeshAnimPath): window("Micro Meshes", glm::ivec2(1024, 1024), &gpuState) {
        if(!gpuState.createDevice(window.getHWND())) {
            gpuState.cleanupDevice();
            UnregisterClassW(window.getWc().lpszClassName, window.getWc().hInstance);
            exit(1);
        }

        const auto dimensions = window.getRenderDimension();

        gpuState.initImGui();
        gpuState.createDepthBuffer(dimensions);

        mesh = GPUMesh::loadGLTFMeshGPU(umeshAnimPath, umeshPath, gpuState.get_device());

        skinningShader = RasterizationShader(L"shaders/skinningVS.hlsl", L"shaders/skinningPS.hlsl", 5, gpuState.get_device());

        //Creating ray tracing shader
        {
            CommandSender cw(gpuState.get_device(), D3D12_COMMAND_LIST_TYPE_DIRECT);

            rtShader = RayTraceShader(
               RESOURCE_ROOT L"shaders/raygen.hlsl",
               RESOURCE_ROOT L"shaders/miss.hlsl",
               RESOURCE_ROOT L"shaders/closesthit.hlsl",
               RESOURCE_ROOT L"shaders/intersection.hlsl",
               {},
               {{SRV, 4}, {UAV, 1}, {CBV, 2}},
               gpuState.get_device()
           );

            rtShader.createAccStrucSRV(mesh[0].getTLASBuffer());


            std::vector<RayTraceVertex> vertices;
            vertices.reserve(mesh[0].cpuMesh.vertices.size());
            std::ranges::transform(mesh[0].cpuMesh.vertices, std::back_inserter(vertices), [](const Vertex& v) { return RayTraceVertex{v.position}; });

            vertexBuffer = DefaultBuffer<RayTraceVertex>(gpuState.get_device(), vertices.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            vertexBuffer.getBuffer()->SetName(L"Vertex buffer (ray tracing)");
            vertexBuffer.upload(vertices, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            rtShader.createSRV<RayTraceVertex>(vertexBuffer.getBuffer());


            std::vector<AABB> AABBs;
            AABBs.reserve(mesh[0].cpuMesh.triangles.size());
            std::vector<glm::vec3> displacements;
            for(const auto& [triangle, AABB] : std::views::zip(mesh[0].cpuMesh.triangles, mesh[0].getAABBs())) {
                //TODO don't forget to not hardcode nRows to 9. Just for testing
                AABBs.emplace_back(glm::vec3{AABB.MinX, AABB.MinY, AABB.MinZ}, glm::vec3{AABB.MaxX, AABB.MaxY, AABB.MaxZ}, triangle.baseVertexIndices, 9, displacements.size());

                std::ranges::transform(triangle.uVertices, std::back_inserter(displacements), [](const uVertex& uv) { return uv.displacement; });
            }

            AABBBuffer = DefaultBuffer<AABB>(gpuState.get_device(), AABBs.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            AABBBuffer.getBuffer()->SetName(L"AABB buffer");
            AABBBuffer.upload(AABBs, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            rtShader.createSRV<AABB>(AABBBuffer.getBuffer());

            disBuffer = DefaultBuffer<glm::vec3>(gpuState.get_device(), displacements.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            disBuffer.getBuffer()->SetName(L"Displacement buffer (ray tracing)");
            disBuffer.upload(displacements, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            rtShader.createSRV<glm::vec3>(disBuffer.getBuffer());


            //Creating output texture
            auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                dimensions.x,
                dimensions.y,
                1,
                1
            );
            texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

            gpuState.get_device()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&raytracingOutput));
            raytracingOutput->SetName(L"Ray Trace Output Texture");

            rtShader.createOutputUAV(raytracingOutput);

            glm::mat4 invViewProj = glm::transpose(glm::inverse(projectionMatrix * trackball->viewMatrix()));

            invViewProjBuffer = UploadBuffer<glm::mat4>(gpuState.get_device(), 1, true);
            invViewProjBuffer.getBuffer()->SetName(L"Inverse view-projection matrix");
            invViewProjBuffer.upload({invViewProj});
            rtShader.createCBV(invViewProjBuffer.getBuffer());

            meshDataBuffer = UploadBuffer<int>(gpuState.get_device(), 1, true);
            meshDataBuffer.getBuffer()->SetName(L"Mesh data buffer");
            meshDataBuffer.upload({3}); //TODO don't hardcode this to 3.
            rtShader.createCBV(meshDataBuffer.getBuffer());

            rtShader.createPipeline();

            cw.execute(gpuState.get_device());
            cw.reset();

            rtShader.createSBT(dimensions.x, dimensions.y);
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
            //gpuState.renderFrame(window.getBackgroundColor(), window.getRenderDimension(), [this] { render(); }, skinningShader);
            gpuState.renderRaytracedScene(rtShader, raytracingOutput);
        }
    }

    ~Application() {
        gpuState.waitForLastSubmittedFrame(); //Wait for the GPU to finish using up all resources before destroying everything
    }

private:
    GPUState gpuState;
    Window window;

    RasterizationShader skinningShader;

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

    ComPtr<ID3D12Resource> raytracingOutput;
    RayTraceShader rtShader;
    UploadBuffer<glm::mat4> invViewProjBuffer;
    UploadBuffer<int> meshDataBuffer;
    DefaultBuffer<AABB> AABBBuffer;
    DefaultBuffer<glm::vec3> disBuffer;
    DefaultBuffer<RayTraceVertex> vertexBuffer;

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
