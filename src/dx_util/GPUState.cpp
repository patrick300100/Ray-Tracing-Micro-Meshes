#include "GPUState.h"

#include <d3dx12.h>
#include <iomanip>
#include <iostream>
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

Microsoft::WRL::ComPtr<IDXGISwapChain3> GPUState::get_swap_chain() const {
    return swapChain;
}

GPUState::~GPUState() {
    waitForLastSubmittedFrame();

    cleanupDevice();
}

bool GPUState::createDevice(const ComPtr<ID3D12Device5>& d, const ComPtr<IDXGISwapChain3>& sc) {
    device = d;
    swapChain = sc;

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = APP_NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if(d->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvDescHeap)) != S_OK) return false;

        SIZE_T rtvDescriptorSize = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
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
        if(d->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvDescHeap)) != S_OK) return false;
        srvDescHeapAlloc.Create(d.Get(), srvDescHeap.Get());
    }

    //Create descriptor heap for depth buffers
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        if(d->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap))!= S_OK) return false;
    }

    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if(d->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)) != S_OK) return false;
    }

    for(UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++) {
        if(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frameContext[i].commandAllocator)) != S_OK) return false;
    }

    if(d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frameContext[0].commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)) != S_OK || commandList->Close() != S_OK) return false;

    if(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) != S_OK) return false;

    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if(fenceEvent == nullptr) return false;

    createRenderTarget();
    return true;
}

void GPUState::cleanupDevice() {
    cleanupRenderTarget();
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

void GPUState::renderFrame(const CommandSender& cs, const glm::vec4& clearColor, const glm::uvec2& dimension, const std::function<void()>& render, const Shader& shader) {
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

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(dimension.x);
    viewport.Height = static_cast<float>(dimension.y);
    viewport.MinDepth = 0.1f;
    viewport.MaxDepth = 1.0f;
    commandList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(dimension.x);
    scissorRect.bottom = static_cast<LONG>(dimension.y);
    commandList->RSSetScissorRects(1, &scissorRect);

    const float clear_color_with_alpha[4] = { clearColor.x * clearColor.w, clearColor.y * clearColor.w, clearColor.z * clearColor.w, clearColor.w };
    commandList->ClearRenderTargetView(mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);

    const auto dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &mainRenderTargetDescriptor[backBufferIdx], FALSE, &dsvHandle);

    ID3D12DescriptorHeap* heap[] = { srvDescHeap.Get() };
    commandList->SetDescriptorHeaps(1, heap);
    commandList->SetGraphicsRootSignature(shader.getRootSignature().Get());

    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &scissorRect);

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
        { "DIRECTION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(Vertex, direction),     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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

void GPUState::createDepthBuffer(const glm::uvec2& dimension) {
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Alignment = 0;
    depthDesc.Width = dimension.x;
    depthDesc.Height = dimension.y;
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
