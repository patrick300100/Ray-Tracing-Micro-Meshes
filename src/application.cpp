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

struct BaseVertex {
    glm::vec3 position;
    glm::vec3 direction;
};

class Application {
public:
    explicit Application(const std::filesystem::path& umeshPath, const bool tessellated):
        window("Micro Meshes", glm::ivec2(1024, 1024), &gpuState),
        projectionMatrix(glm::perspective(glm::radians(80.0f), window.getAspectRatio(), 0.1f, 1000.0f)),
        runTessellated(tessellated)
    {
        createDevice();

        swapChainCS = CommandSender(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
        createSwapchain(window.getHWND());

        if(!gpuState.createDevice(device, swapChain)) {
            gpuState.cleanupDevice();
            UnregisterClassW(window.getWc().lpszClassName, window.getWc().hInstance);
            exit(1);
        }

        const auto dimensions = window.getRenderDimension();

        mesh = GPUMesh::loadGLTFMeshGPU(umeshPath, device, runTessellated);

        CommandSender cw(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
        cw.reset();

        if(runTessellated) { //We tessellate the micro-mesh and upload the mesh as a regular mesh to the ray tracer
            rtShader = RayTraceShader(
               RESOURCE_ROOT L"shaders/raygen.hlsl",
               RESOURCE_ROOT L"shaders/miss.hlsl",
               RESOURCE_ROOT L"shaders/closesthitTriangle.hlsl",
               RESOURCE_ROOT L"shaders/intersection.hlsl", //We will not use this one
               {},
               {{SRV, 3}, {UAV, 1}, {CBV, 1}},
               device,
               mesh[0].cpuMesh.hasUniformSubdivisionLevel()
            );

            rtShader.createAccStrucSRV(mesh[0].getTLASBuffer());

            rtShader.createSRV<Vertex>(mesh[0].getVertexBuffer());
            rtShader.createSRV<glm::uvec3>(mesh[0].getIndexBuffer());


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

            device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&raytracingOutput));

            rtShader.createOutputUAV(raytracingOutput);


            invViewProjBuffer = UploadBuffer<glm::mat4>(device, 1, true);
            rtShader.createCBV(invViewProjBuffer.getBuffer());

            rtShader.createTrianglePipeline();
            rtShader.createSBT(dimensions.x, dimensions.y, cw.getCommandList());

            cw.execute(device);
            cw.reset();
        } else { //Ray trace the micro-mesh
            rtShader = RayTraceShader(
               RESOURCE_ROOT L"shaders/raygen.hlsl",
               RESOURCE_ROOT L"shaders/miss.hlsl",
               RESOURCE_ROOT L"shaders/closesthit.hlsl",
               RESOURCE_ROOT L"shaders/intersection.hlsl",
               {},
               {{SRV, 6}, {UAV, 1}, {CBV, 1}},
               device,
               mesh[0].cpuMesh.hasUniformSubdivisionLevel()
            );

            rtShader.createAccStrucSRV(mesh[0].getTLASBuffer());


            std::vector<BaseVertex> baseVertices;
            baseVertices.reserve(mesh[0].cpuMesh.vertices.size());
            std::ranges::transform(mesh[0].cpuMesh.vertices, std::back_inserter(baseVertices), [](const Vertex& v) { return BaseVertex{v.position, v.direction}; });

            vertexBuffer = DefaultBuffer<BaseVertex>(device, baseVertices.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            vertexBuffer.upload(baseVertices, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            rtShader.createSRV<BaseVertex>(vertexBuffer.getBuffer());

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
            auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                dimensions.x,
                dimensions.y,
                1,
                1
            );
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

            glm::mat4 invViewProj = glm::inverse(projectionMatrix * trackball->viewMatrix());

            invViewProjBuffer = UploadBuffer<glm::mat4>(device, 1, true);
            invViewProjBuffer.upload({invViewProj});
            rtShader.createCBV(invViewProjBuffer.getBuffer());

            rtShader.createPipeline();
            rtShader.createSBT(dimensions.x, dimensions.y, cw.getCommandList());

            cw.execute(device);
            cw.reset();
        }
    }

    void update() {
        while (window.shouldClose()) {
            window.updateInput();

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

    std::vector<GPUMesh> mesh;

    std::unique_ptr<Trackball> trackball = std::make_unique<Trackball>(&window, glm::radians(50.0f));

    glm::mat4 projectionMatrix;

    ComPtr<ID3D12Resource> raytracingOutput;
    RayTraceShader rtShader;
    UploadBuffer<glm::mat4> invViewProjBuffer;
    DefaultBuffer<TriangleData> triangleData;
    DefaultBuffer<float> displacementScalesBuffer;
    DefaultBuffer<BaseVertex> vertexBuffer;
    DefaultBuffer<glm::vec2> minMaxDisplacementBuffer;
    DefaultBuffer<float> deltaBuffer;

    bool runTessellated;

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
    //The first argument is the path to the .exe file
    if(argc == 1) {
        std::cerr << "Did not specify micro mesh file as program argument.";
        return 1;
    }

    //Introducing scope to make destroy the Application object before we check for live objects
    {
        const std::filesystem::path umeshPath(argv[1]);
        if(!std::filesystem::exists(umeshPath)) {
            std::cerr << "Micro-mesh file does not exist.";
            return 1;
        }

        bool tessellated = false;
        if(argc == 3 && std::string(argv[2]) == "-T") tessellated = true;

        Application app(umeshPath, tessellated);
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
