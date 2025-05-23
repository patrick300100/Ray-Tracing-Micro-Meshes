#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui/imgui.h>
#include <wrl/client.h>
#include <functional>
#include <glm/vec2.hpp>

#include "shader.h"

struct FrameContext {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    UINT64 fenceValue;
};

// Simple free list based allocator
struct ExampleDescriptorHeapAllocator {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> Heap;
    D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
    UINT                        HeapHandleIncrement;
    ImVector<int>               FreeIndices;

    void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap) {
        IM_ASSERT(Heap == nullptr && FreeIndices.empty());
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapType = desc.Type;
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for(int n = desc.NumDescriptors; n > 0; n--) FreeIndices.push_back(n - 1);
    }

    void Destroy() {
        Heap = nullptr;
        FreeIndices.clear();
    }

    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle) {
        IM_ASSERT(FreeIndices.Size > 0);
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }

    void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle) {
        int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        IM_ASSERT(cpu_idx == gpu_idx);
        FreeIndices.push_back(cpu_idx);
    }
};

class GPUState {
    static constexpr int APP_NUM_FRAMES_IN_FLIGHT = 2;
    static constexpr int APP_NUM_BACK_BUFFERS = 2;
    static constexpr int APP_SRV_HEAP_SIZE = 64;

    FrameContext frameContext[APP_NUM_FRAMES_IN_FLIGHT] = {};
    UINT frameIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12Device> device;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ExampleDescriptorHeapAllocator srvDescHeapAlloc{};

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceLastSignaledValue = 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
    bool swapChainOccluded = false;
    HANDLE swapChainWaitableObject = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> mainRenderTargetResource[APP_NUM_BACK_BUFFERS] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE mainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS] = {};

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;

    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer;

public:
    ~GPUState();

    bool createDevice(HWND hWnd);
    void cleanupDevice();
    void createRenderTarget();
    void cleanupRenderTarget();
    void waitForLastSubmittedFrame();
    FrameContext* waitForNextFrameResources();

    void initImGui() const;

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Device> get_device() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<IDXGISwapChain3> get_swap_chain() const;

    void renderFrame(const ImVec4& clearColor, const std::function<void()>& render, const glm::ivec2& windowSize);

    void createDepthBuffer();
    void createPipeline(const Shader& shaders);

    void drawMesh(D3D12_VERTEX_BUFFER_VIEW vbv, D3D12_INDEX_BUFFER_VIEW ibv, UINT nIndices) const;

    void setConstantBuffer(UINT index, const Microsoft::WRL::ComPtr<ID3D12Resource>& bufferPtr) const;
};
