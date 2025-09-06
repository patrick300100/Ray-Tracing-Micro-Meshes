#include <dxgidebug.h>

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
#include "TriangleData.h"

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#pragma comment(lib, "dxguid.lib")
#endif

struct RayTraceVertex {
    glm::vec3 position;
    glm::vec3 direction;
};

class Application {
public:
    Application(const std::filesystem::path& umeshPath, const std::filesystem::path& umeshAnimPath): window("Micro Meshes", glm::ivec2(1024, 1024), &gpuState) {
        createDevice();

        swapChainCS = CommandSender(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
        createSwapchain(window.getHWND());

        if(!gpuState.createDevice(device, swapChain)) {
            gpuState.cleanupDevice();
            UnregisterClassW(window.getWc().lpszClassName, window.getWc().hInstance);
            exit(1);
        }

        const auto dimensions = window.getRenderDimension();

        gpuState.initImGui();
        gpuState.createDepthBuffer(dimensions);

        mesh = GPUMesh::loadGLTFMeshGPU(umeshAnimPath, umeshPath, device);

        skinningShader = RasterizationShader(L"shaders/skinningVS.hlsl", L"shaders/skinningPS.hlsl", 5, device);

        CommandSender cw(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
        cw.reset();

        //Creating ray tracing shader
        {
            rtShader = RayTraceShader(
               RESOURCE_ROOT L"shaders/raygen.hlsl",
               RESOURCE_ROOT L"shaders/miss.hlsl",
               RESOURCE_ROOT L"shaders/closesthit.hlsl",
               RESOURCE_ROOT L"shaders/intersection.hlsl",
               {},
               {{SRV, 6}, {UAV, 2}, {CBV, 1}},
               device,
               mesh[0].cpuMesh.hasUniformSubdivisionLevel()
           );

            rtShader.createAccStrucSRV(mesh[0].getTLASBuffer());


            std::vector<RayTraceVertex> vertices;
            vertices.reserve(mesh[0].cpuMesh.vertices.size());
            std::ranges::transform(mesh[0].cpuMesh.vertices, std::back_inserter(vertices), [](const Vertex& v) { return RayTraceVertex{v.position, v.direction}; });

            vertexBuffer = DefaultBuffer<RayTraceVertex>(device, vertices.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            vertexBuffer.upload(vertices, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            rtShader.createSRV<RayTraceVertex>(vertexBuffer.getBuffer());

            const auto cpuMesh = mesh[0].cpuMesh;

            std::vector<TriangleData> tData;
            tData.reserve(cpuMesh.triangles.size());
            const auto displacementScales = cpuMesh.computeDisplacementScales(tData);

            const auto minMaxDisplacements = cpuMesh.minMaxDisplacements(tData);

            triangleData = DefaultBuffer<TriangleData>(device, tData.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            triangleData.upload(tData, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            rtShader.createSRV<TriangleData>(triangleData.getBuffer());

            displacementScalesBuffer = DefaultBuffer<float>(device, displacementScales.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            displacementScalesBuffer.upload(displacementScales, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            rtShader.createSRV<float>(displacementScalesBuffer.getBuffer());

            minMaxDisplacementBuffer = DefaultBuffer<glm::vec2>(device, minMaxDisplacements.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            minMaxDisplacementBuffer.upload(minMaxDisplacements, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            rtShader.createSRV<glm::vec2>(minMaxDisplacementBuffer.getBuffer());

            std::vector<int> allOffsets;
            std::ranges::transform(tData, std::back_inserter(allOffsets), [&](const TriangleData& td) { return td.displacementOffset; });
            const auto deltas = cpuMesh.triangleDeltas(allOffsets);

            deltaBuffer = DefaultBuffer<float>(device, deltas.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            deltaBuffer.upload(deltas, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            rtShader.createSRV<float>(deltaBuffer.getBuffer());


            //Creating output texture
            auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, dimensions.x, dimensions.y, 1, 1);
            texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

            device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&raytracingOutput));

            rtShader.createOutputUAV(raytracingOutput);

            //Create screenshot texture
            auto screenshotTexDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 3840, 2160, 1, 1);
            screenshotTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &screenshotTexDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&screenshotOutput));

            rtShader.createOutputUAV(screenshotOutput);

            glm::mat4 invViewProj = glm::inverse(projectionMatrix * trackball->viewMatrix());

            invViewProjBuffer = UploadBuffer<glm::mat4>(device, 1, true);
            invViewProjBuffer.upload({invViewProj});
            rtShader.createCBV(invViewProjBuffer.getBuffer());

            rtShader.createPipeline();
            rtShader.createSBT(dimensions.x, dimensions.y, cw.getCommandList());

            cw.execute(device);
            cw.reset();
        }

        //Creating ray tracing shader that ray traces without AABBs
        // {
        //     rtTriangleShader = RayTraceShader(
        //        RESOURCE_ROOT L"shaders/raygen.hlsl",
        //        RESOURCE_ROOT L"shaders/miss.hlsl",
        //        RESOURCE_ROOT L"shaders/closesthitTriangle.hlsl",
        //        RESOURCE_ROOT L"shaders/intersection.hlsl", //We will not use this one
        //        {},
        //        {{SRV, 3}, {UAV, 1}, {CBV, 1}},
        //        device
        //     );
        //
        //     rtTriangleShader.createAccStrucSRV(mesh[0].getTLASBuffer());
        //
        //     const auto [vData, iData] = mesh[0].cpuMesh.allTriangles();
        //     rtTriangleShader.createSRV<Vertex>(mesh[0].getVertexBuffer());
        //     rtTriangleShader.createSRV<glm::uvec3>(mesh[0].getIndexBuffer());
        //
        //
        //     //Creating output texture
        //     auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        //         DXGI_FORMAT_R8G8B8A8_UNORM,
        //         dimensions.x,
        //         dimensions.y,
        //         1,
        //         1
        //     );
        //     texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        //
        //     const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        //
        //     device->CreateCommittedResource(
        //         &heapProps,
        //         D3D12_HEAP_FLAG_NONE,
        //         &texDesc,
        //         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        //         nullptr,
        //         IID_PPV_ARGS(&raytracingOutput));
        //
        //     rtTriangleShader.createOutputUAV(raytracingOutput);
        //
        //
        //     invViewProjBuffer = UploadBuffer<glm::mat4>(device, 1, true);
        //     rtTriangleShader.createCBV(invViewProjBuffer.getBuffer());
        //
        //     rtTriangleShader.createTrianglePipeline();
        //     rtTriangleShader.createSBT(dimensions.x, dimensions.y, cw.getCommandList());
        //
        //     cw.execute(device);
        //     cw.reset();
        // }

        gpuState.createPipeline(skinningShader);

        boneBuffer = UploadBuffer<glm::mat4>(device, 10, true);
        mvpBuffer = UploadBuffer<glm::mat4>(device, 1, true);
        mvBuffer = UploadBuffer<glm::mat4>(device, 1, true);
        displacementBuffer = UploadBuffer<float>(device, 1, true);
        cameraPosBuffer = UploadBuffer<glm::vec3>(device, 1, true);

        //When user presses 'C', we want to take a screenshot of the current frame
        window.registerKeyCallback([this](int key, int scancode, int action, int mods) {
            if(key == 'C' && action == WM_KEYUP) takeScreenshot = true;
        });
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
            //Window::prepareFrame();

            //if(!gui.animation.pause) gui.animation.time = std::fmod(getTime(), mesh[0].cpuMesh.animationDuration());

            //menu();
            //gpuState.renderFrame(window.getBackgroundColor(), window.getRenderDimension(), [this] { render(); }, skinningShader);

            glm::mat4 invViewProj = glm::inverse(projectionMatrix * trackball->viewMatrix());
            invViewProjBuffer.upload({invViewProj});

            swapChainCS.reset();

            swapChainCS.getCommandList()->SetPipelineState1(rtShader.pipelineStateObject.Get());
            swapChainCS.getCommandList()->SetComputeRootSignature(rtShader.globalRootSignature.Get());
            swapChainCS.getCommandList()->SetDescriptorHeaps(1, rtShader.descriptorHeap.GetAddressOf());
            swapChainCS.getCommandList()->SetComputeRootDescriptorTable(0, rtShader.descriptorHeap->GetGPUDescriptorHandleForHeapStart());

            swapChainCS.getCommandList()->DispatchRays(&rtShader.dispatchDesc);

            //Getting the back buffer
            ComPtr<ID3D12Resource> backBuffer;
            swapChain->GetBuffer(swapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&backBuffer));

            //Lambda function that creates a transition barrier and puts it into the command list
            auto barrier = [&](auto* resource, auto before, auto after) {
                auto rb = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
                swapChainCS.getCommandList()->ResourceBarrier(1, &rb);
            };

            //Wait 'till we're sure that all the results are written to the output texture
            auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(raytracingOutput.Get());
            swapChainCS.getCommandList()->ResourceBarrier(1, &uavBarrier);

            barrier(raytracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            barrier(backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);

            swapChainCS.getCommandList()->CopyResource(backBuffer.Get(), raytracingOutput.Get());

            barrier(backBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
            barrier(raytracingOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            swapChainCS.execute(device);

            swapChain->Present(1, 0);
        }
    }

    ~Application() {
        gpuState.waitForLastSubmittedFrame(); //Wait for the GPU to finish using up all resources before destroying everything
    }

private:
    ComPtr<ID3D12Device5> device;

    GPUState gpuState;
    Window window;

    ComPtr<IDXGISwapChain3> swapChain;
    CommandSender swapChainCS; //Command queue and such for the swapchain

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
    ComPtr<ID3D12Resource> screenshotOutput;
    RayTraceShader rtShader, rtTriangleShader;
    UploadBuffer<glm::mat4> invViewProjBuffer;
    DefaultBuffer<TriangleData> triangleData;
    DefaultBuffer<float> displacementScalesBuffer;
    DefaultBuffer<RayTraceVertex> vertexBuffer;
    DefaultBuffer<glm::vec2> minMaxDisplacementBuffer;
    DefaultBuffer<float> deltaBuffer;

    bool takeScreenshot = false;

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

    void createDevice() {
#ifdef DX12_ENABLE_DEBUG_LAYER
        ID3D12Debug1* pdx12Debug = nullptr;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug)))) pdx12Debug->EnableDebugLayer();
#endif

        D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));

#ifdef DX12_ENABLE_DEBUG_LAYER
        if(pdx12Debug != nullptr) {
            ID3D12InfoQueue* pInfoQueue = nullptr;
            device->QueryInterface(IID_PPV_ARGS(&pInfoQueue));

            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

            D3D12_MESSAGE_ID denyIDs[] = {
                D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED, //Harmless error about default heap
            };

            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = _countof(denyIDs);
            filter.DenyList.pIDList = denyIDs;
            pInfoQueue->AddStorageFilterEntries(&filter);

            pInfoQueue->Release();
            pdx12Debug->Release();
        }
#endif
    }

    void createSwapchain(HWND hWnd) {
        // Setup swap chain
        DXGI_SWAP_CHAIN_DESC1 sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;

        IDXGIFactory4* dxgiFactory = nullptr;
        IDXGISwapChain1* swapChain1 = nullptr;
        CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
        dxgiFactory->CreateSwapChainForHwnd(swapChainCS.getCommandQueue().Get(), hWnd, &sd, nullptr, nullptr, &swapChain1);
        swapChain1->QueryInterface(IID_PPV_ARGS(&swapChain));
        swapChain1->Release();
        dxgiFactory->Release();
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


#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = nullptr;
    if(SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug)))) {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        pDebug->Release();
    }
#endif

    return 0;
}
