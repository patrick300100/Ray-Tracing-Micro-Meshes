#include "GPUState.h"

#include <d3dx12.h>
#include <../../framework/include/framework/mesh.h>
#include <../../framework/third_party/imgui/include/imgui/imgui_impl_dx12.h>
#include "../../src/dx_util/RasterizationShader.h"

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

Microsoft::WRL::ComPtr<ID3D12Device> GPUState::get_device() const {
    return device;
}

Microsoft::WRL::ComPtr<IDXGISwapChain3> GPUState::get_swap_chain() const {
    return swapChain;
}

GPUState::~GPUState() {
    waitForLastSubmittedFrame();

    cleanupDevice();
}

bool GPUState::createDevice(const HWND hWnd) {
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = APP_NUM_BACK_BUFFERS;
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
    }

    // [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = nullptr;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    // Create device
    if(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)) != S_OK) return false;

    // [DEBUG] Setup debug interface to break on any warnings/errors
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

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = APP_NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvDescHeap)) != S_OK) return false;

        SIZE_T rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        for(UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++) {
            mainRenderTargetDescriptor[i] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = APP_SRV_HEAP_SIZE;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvDescHeap)) != S_OK) return false;
        srvDescHeapAlloc.Create(device.Get(), srvDescHeap.Get());
    }

    //Create descriptor heap for depth buffers
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        if(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap))!= S_OK) return false;
    }

    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)) != S_OK) return false;
    }

    for(UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++) {
        if(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frameContext[i].commandAllocator)) != S_OK) return false;
    }

    if(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frameContext[0].commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)) != S_OK || commandList->Close() != S_OK) return false;

    if(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) != S_OK) return false;

    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if(fenceEvent == nullptr) return false;

    {
        IDXGIFactory4* dxgiFactory = nullptr;
        IDXGISwapChain1* swapChain1 = nullptr;
        if(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK) return false;
        if(dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK) return false;
        if(swapChain1->QueryInterface(IID_PPV_ARGS(&swapChain)) != S_OK) return false;
        swapChain1->Release();
        dxgiFactory->Release();
        swapChain->SetMaximumFrameLatency(APP_NUM_BACK_BUFFERS);
        swapChainWaitableObject = swapChain->GetFrameLatencyWaitableObject();
    }

    createRenderTarget();
    return true;
}

void GPUState::cleanupDevice() {
    cleanupRenderTarget();
    if(swapChain) {
        swapChain->SetFullscreenState(false, nullptr);
        swapChain.Reset();
    }
    if(swapChainWaitableObject != nullptr) { CloseHandle(swapChainWaitableObject); }
    for(auto& fc : frameContext) {
        if(fc.commandAllocator) fc.commandAllocator.Reset();
    }
    if(commandQueue) commandQueue.Reset();
    if(commandList) commandList.Reset();
    if(rtvDescHeap) rtvDescHeap.Reset();
    if(srvDescHeap) srvDescHeap.Reset();
    if(dsvHeap) dsvHeap.Reset();
    srvDescHeapAlloc.Destroy();
    if(fence) fence.Reset();
    if(fenceEvent) { CloseHandle(fenceEvent); fenceEvent = nullptr; }
    if(pipeline) pipeline.Reset();
    if(depthStencilBuffer) depthStencilBuffer.Reset();
    if(device) device.Reset();

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = nullptr;
    if(SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug)))) {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        pDebug->Release();
    }
#endif
}

void GPUState::createRenderTarget() {
    for(UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++) {
        swapChain->GetBuffer(i, IID_PPV_ARGS(&mainRenderTargetResource[i]));
        device->CreateRenderTargetView(mainRenderTargetResource[i].Get(), nullptr, mainRenderTargetDescriptor[i]);
    }
}

void GPUState::waitForLastSubmittedFrame() {
    FrameContext* frameCtx = &frameContext[frameIndex % APP_NUM_FRAMES_IN_FLIGHT];

    UINT64 fenceValue = frameCtx->fenceValue;
    if(fenceValue == 0) return; // No fence was signaled

    frameCtx->fenceValue = 0;
    if(fence->GetCompletedValue() >= fenceValue) return;

    fence->SetEventOnCompletion(fenceValue, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
}

FrameContext* GPUState::waitForNextFrameResources() {
    UINT nextFrameIndex = frameIndex + 1;
    frameIndex = nextFrameIndex;

    HANDLE waitableObjects[] = { swapChainWaitableObject, nullptr };
    DWORD numWaitableObjects = 1;

    FrameContext* frameCtx = &frameContext[nextFrameIndex % APP_NUM_FRAMES_IN_FLIGHT];
    UINT64 fenceValue = frameCtx->fenceValue;
    if(fenceValue != 0) // means no fence was signaled
    {
        frameCtx->fenceValue = 0;
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        waitableObjects[1] = fenceEvent;
        numWaitableObjects = 2;
    }

    WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

    return frameCtx;
}

void GPUState::cleanupRenderTarget() {
    waitForLastSubmittedFrame();

    for(auto& rt : mainRenderTargetResource) {
        if(rt) rt.Reset();
    }
}

void GPUState::initImGui() const {
    // Before 1.91.6: our signature was using a single descriptor. From 1.92, specifying SrvDescriptorAllocFn/SrvDescriptorFreeFn will be required to benefit from new features.
    ImGui_ImplDX12_Init(device.Get(), APP_NUM_FRAMES_IN_FLIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, srvDescHeap.Get(), srvDescHeap->GetCPUDescriptorHandleForHeapStart(), srvDescHeap->GetGPUDescriptorHandleForHeapStart());
}

void GPUState::renderFrame(const ImVec4& clearColor, const std::function<void()>& render, const Shader& shader) {
    FrameContext* frameCtx = waitForNextFrameResources();
    const UINT backBufferIdx = swapChain->GetCurrentBackBufferIndex();
    frameCtx->commandAllocator->Reset();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = mainRenderTargetResource[backBufferIdx].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->Reset(frameCtx->commandAllocator.Get(), nullptr);
    commandList->ResourceBarrier(1, &barrier);

    auto rtDesc = mainRenderTargetResource[backBufferIdx]->GetDesc();

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(rtDesc.Width);
    viewport.Height = static_cast<float>(rtDesc.Height);
    viewport.MinDepth = 0.1f;
    viewport.MaxDepth = 1.0f;
    commandList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(rtDesc.Width);
    scissorRect.bottom = static_cast<LONG>(rtDesc.Height);
    commandList->RSSetScissorRects(1, &scissorRect);

    const float clear_color_with_alpha[4] = { clearColor.x * clearColor.w, clearColor.y * clearColor.w, clearColor.z * clearColor.w, clearColor.w };
    commandList->ClearRenderTargetView(mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);

    const auto dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &mainRenderTargetDescriptor[backBufferIdx], FALSE, &dsvHandle);

    ID3D12DescriptorHeap* heap[] = { srvDescHeap.Get() };
    commandList->SetDescriptorHeaps(1, heap);
    commandList->SetGraphicsRootSignature(shader.getRootSignature().Get());

    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    render(); //Render mesh and other drawable objects

    //Render ImGui
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    commandList->ResourceBarrier(1, &barrier);
    commandList->Close();

    ID3D12CommandList* cmdsList[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, cmdsList);

    // Present
    HRESULT hr = swapChain->Present(1, 0); // Present with vsync
    //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
    swapChainOccluded = hr == DXGI_STATUS_OCCLUDED;

    UINT64 fenceValue = fenceLastSignaledValue + 1;
    commandQueue->Signal(fence.Get(), fenceValue);
    fenceLastSignaledValue = fenceValue;
    frameCtx->fenceValue = fenceValue;
}

void GPUState::createPipeline(const RasterizationShader& shaders) {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(Vertex, position),         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",         0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(Vertex, normal),           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BONEINDICES",    0, DXGI_FORMAT_R32G32B32A32_SINT,  0, offsetof(Vertex, boneIndices),      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BONEWEIGHTS",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, boneWeights),      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "DISPLACEMENT",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(Vertex, displacement),     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BASEBONEINDICES", 0, DXGI_FORMAT_R32G32B32A32_SINT,  0, offsetof(Vertex, baseBoneIndices0), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BASEBONEINDICES", 1, DXGI_FORMAT_R32G32B32A32_SINT,  0, offsetof(Vertex, baseBoneIndices1), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BASEBONEINDICES", 2, DXGI_FORMAT_R32G32B32A32_SINT,  0, offsetof(Vertex, baseBoneIndices2), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BASEBONEWEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, baseBoneWeights0), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BASEBONEWEIGHTS", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, baseBoneWeights1), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BASEBONEWEIGHTS", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, baseBoneWeights2), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BARYCOORDS",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(Vertex, baryCoords),       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depthStencilDesc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = shaders.getRootSignature().Get();
    psoDesc.VS = { shaders.getVertexShader()->GetBufferPointer(), shaders.getVertexShader()->GetBufferSize() };
    psoDesc.PS = { shaders.getPixelShader()->GetBufferPointer(), shaders.getPixelShader()->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline));
}

void GPUState::drawMesh(const D3D12_VERTEX_BUFFER_VIEW vbv, const D3D12_INDEX_BUFFER_VIEW ibv, const UINT nIndices) const {
    commandList->SetPipelineState(pipeline.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vbv);
    commandList->IASetIndexBuffer(&ibv);
    commandList->DrawIndexedInstanced(nIndices, 1, 0, 0, 0);
}

void GPUState::setConstantBuffer(const UINT index, const Microsoft::WRL::ComPtr<ID3D12Resource>& bufferPtr) const {
    commandList->SetGraphicsRootConstantBufferView(index, bufferPtr->GetGPUVirtualAddress());
}

void GPUState::createDepthBuffer() {
    D3D12_RESOURCE_DESC backBufferDesc = mainRenderTargetResource[0]->GetDesc();

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Alignment = 0;
    depthDesc.Width = backBufferDesc.Width;
    depthDesc.Height = backBufferDesc.Height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&depthStencilBuffer));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    device->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

glm::uvec2 GPUState::getRenderDimension() const {
    const UINT backBufferIdx = swapChain->GetCurrentBackBufferIndex();
    const auto dim = mainRenderTargetResource[backBufferIdx]->GetDesc();

    return {dim.Width, dim.Height};
}
