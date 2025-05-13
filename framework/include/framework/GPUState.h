#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui/imgui.h>
#include <wrl/client.h>
#include <functional>

struct FrameContext {
    ID3D12CommandAllocator* commandAllocator;
    UINT64 fenceValue;
};

// Simple free list based allocator
struct ExampleDescriptorHeapAllocator {
    ID3D12DescriptorHeap*       Heap = nullptr;
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

    ID3D12Device* device = nullptr;

    ID3D12DescriptorHeap* rtvDescHeap = nullptr;
    ID3D12DescriptorHeap* srvDescHeap = nullptr;
    ExampleDescriptorHeapAllocator srvDescHeapAlloc{};

    ID3D12CommandQueue* commandQueue = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;

    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceLastSignaledValue = 0;

    IDXGISwapChain3* swapChain = nullptr;
    bool swapChainOccluded = false;
    HANDLE swapChainWaitableObject = nullptr;

    ID3D12Resource* mainRenderTargetResource[APP_NUM_BACK_BUFFERS] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE mainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS] = {};

public:
    ~GPUState();

    bool createDevice(HWND hWnd);
    void cleanupDevice();
    void createRenderTarget();
    void cleanupRenderTarget();
    void waitForLastSubmittedFrame();
    FrameContext* waitForNextFrameResources();

    void initImGui() const;

    [[nodiscard]] ID3D12Device* get_device() const;
    [[nodiscard]] IDXGISwapChain3* get_swap_chain() const;

    void renderFrame(const ImVec4& clearColor, const std::function<void()>& render);
};
