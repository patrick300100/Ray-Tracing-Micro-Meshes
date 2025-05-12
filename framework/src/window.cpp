#include "window.h"
#include <imgui/imgui.h>
#undef IMGUI_IMPL_OPENGL_LOADER_GLEW
#define IMGUI_IMPL_OPENGL_LOADER_GLAD 1
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_dx12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windowsx.h>
#include <glm/vec2.hpp>
#include <stb/stb_image_write.h>

std::wstring Window::convertToWString(const std::string_view str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), wstr.data(), size_needed);
    return wstr;
}

Window::Window(std::string_view title, const glm::ivec2& windowSize) {
    // std::string_view does not guarantee that the string contains a terminator character.
    const auto titleString = convertToWString(title);

    // Create application window
    wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"MicroMeshWindow", nullptr };
    RegisterClassExW(&wc);
    hwnd = CreateWindowW(wc.lpszClassName, titleString.c_str(), WS_OVERLAPPEDWINDOW, 100, 100, windowSize.x, windowSize.y, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if(!gpuState.createDevice(hwnd)) {
        gpuState.cleanupDevice();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        exit(1);
    }

    // Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);

    gpuState.initImGui();

    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

Window::~Window() {
    gpuState.waitForLastSubmittedFrame();

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

bool Window::shouldClose() const {
    return !done;
}

void Window::updateInput() {
    // Poll and handle messages (inputs, window resize, etc.)
    // See the WndProc() function below for our to dispatch events to the Win32 backend.
    MSG msg;
    while(::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if(msg.message == WM_QUIT) done = true;
    }

    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Window::swapBuffers() {
    ImGui::Render();

    FrameContext* frameCtx = gpuState.waitForNextFrameResources();;
    UINT backBufferIdx = gpuState.get_swap_chain()->GetCurrentBackBufferIndex();
    frameCtx->commandAllocator->Reset();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = gpuState.getMainRenderTargetResource(backBufferIdx);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    gpuState.get_command_list()->Reset(frameCtx->commandAllocator, nullptr);
    gpuState.get_command_list()->ResourceBarrier(1, &barrier);

    // Render Dear ImGui graphics
    const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
    gpuState.get_command_list()->ClearRenderTargetView(gpuState.getMainRenderTargetDescriptor(backBufferIdx), clear_color_with_alpha, 0, nullptr);
    auto targetDesc = gpuState.getMainRenderTargetDescriptor(backBufferIdx);
    gpuState.get_command_list()->OMSetRenderTargets(1, &targetDesc, FALSE, nullptr);
    auto srvHeap = gpuState.get_srv_heap();
    gpuState.get_command_list()->SetDescriptorHeaps(1, &srvHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gpuState.get_command_list());
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    gpuState.get_command_list()->ResourceBarrier(1, &barrier);
    gpuState.get_command_list()->Close();

    auto cmdl = gpuState.get_command_list();
    gpuState.get_command_queue()->ExecuteCommandLists(1, (ID3D12CommandList* const*)&cmdl);

    // Present
    HRESULT hr = gpuState.get_swap_chain()->Present(1, 0);   // Present with vsync
    //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
    gpuState.setSwapChainOccluded(hr == DXGI_STATUS_OCCLUDED);

    UINT64 fenceValue = gpuState.get_fence_last_signaled_value() + 1;
    gpuState.get_command_queue()->Signal(gpuState.get_fence(), fenceValue);
    gpuState.setFenceLastSignaledValue(fenceValue);
    frameCtx->fenceValue = fenceValue;
}

void Window::renderToImage (const std::filesystem::path& filePath, const bool flipY) {
    std::vector <GLubyte> pixels;
    pixels.reserve (4 * m_windowSize.x * m_windowSize.y);

    glReadPixels(0, 0, m_windowSize.x, m_windowSize.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    std::string filePathString = filePath.string();

    // flips Y axis
    if(flipY) {
        // swap entire lines (if height is odd will not touch middle line)
        for(int line = 0; line != m_windowSize.y/2; ++line) {
               std::swap_ranges(pixels.begin() + 4 * m_windowSize.x * line,
                pixels.begin() + 4 * m_windowSize.x * (line + 1),
                pixels.begin() + 4 * m_windowSize.x * (m_windowSize.y - line - 1));
        }
    }

    if((filePath.extension()).compare(".bmp") == 0) {
        stbi_write_bmp(filePathString.c_str(), m_windowSize.x, m_windowSize.y, 4, pixels.data());
    }
    else if((filePath.extension()).compare(".png") == 0) {
        stbi_write_png(filePathString.c_str(), m_windowSize.x, m_windowSize.y, 4, pixels.data(), 4*m_windowSize.x);
    }
}

void Window::registerKeyCallback(KeyCallback&& callback) {
    m_keyCallbacks.push_back(std::move(callback));
}

void Window::registerCharCallback(CharCallback&& callback) {
    m_charCallbacks.push_back(std::move(callback));
}

void Window::registerMouseButtonCallback(MouseButtonCallback&& callback) {
    m_mouseButtonCallbacks.push_back(std::move(callback));
}

void Window::registerScrollCallback(ScrollCallback&& callback) {
    m_scrollCallbacks.push_back(std::move(callback));
}

void Window::registerWindowResizeCallback(WindowResizeCallback&& callback) {
    m_windowResizeCallbacks.push_back(std::move(callback));
}

void Window::registerMouseMoveCallback(MouseMoveCallback&& callback) {
    m_mouseMoveCallbacks.push_back(std::move(callback));
}

bool Window::isKeyPressed(int key) const {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool Window::isMouseButtonPressed(int button) const {
    int vkButton;
    switch (button) {
        case 0: vkButton = VK_LBUTTON; break;
        case 1: vkButton = VK_RBUTTON; break;
        case 2: vkButton = VK_MBUTTON; break;
        default: return false;
    }

    return (GetAsyncKeyState(vkButton) & 0x8000) != 0;
}

glm::vec2 Window::getCursorPos() const{
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(hwnd, &p); // m_hWnd is your HWND stored in the Window class
    return glm::vec2(static_cast<float>(p.x), static_cast<float>(m_windowSize.y - 1 - p.y));
}

glm::vec2 Window::getNormalizedCursorPos() const {
    return getCursorPos() / glm::vec2(m_windowSize);
}

glm::ivec2 Window::getWindowSize() const {
    return m_windowSize;
}

float Window::getAspectRatio() const {
    if(m_windowSize.x == 0 || m_windowSize.y == 0) return 1.0f;
    return float(m_windowSize.x) / float(m_windowSize.y);
}

float Window::getDpiScalingFactor() const {
    return m_dpiScalingFactor;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI Window::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if(ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    auto* pWindow = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if(!pWindow) return DefWindowProcW(hWnd, msg, wParam, lParam);

    const ImGuiIO& io = ImGui::GetIO();

    switch(msg) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            if(io.WantCaptureKeyboard) break;

            int action = (msg == WM_KEYDOWN) ? GLFW_PRESS : GLFW_RELEASE;
            int key = static_cast<int>(wParam);
            int scancode = 0; // Optional: can use MapVirtualKey if needed
            int mods = 0;     // Optional: get with GetKeyState(VK_SHIFT), etc.

            for(const auto& cb : pWindow->m_keyCallbacks) cb(key, scancode, action, mods);
            break;
        }

        case WM_CHAR:
        {
            if(io.WantCaptureKeyboard) break;

            auto codepoint = static_cast<unsigned>(wParam);
            for(const auto& cb : pWindow->m_charCallbacks) cb(codepoint);
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        {
            if(io.WantCaptureMouse) break;

            int button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) ? GLFW_MOUSE_BUTTON_LEFT : GLFW_MOUSE_BUTTON_RIGHT;
            int action = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) ? GLFW_PRESS : GLFW_RELEASE;
            int mods = 0; // Optional modifier keys

            for(const auto& cb : pWindow->m_mouseButtonCallbacks) cb(button, action, mods);
            break;
        }

        case WM_MOUSEMOVE:
        {
            if(io.WantCaptureMouse) break;

            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            glm::vec2 pos = glm::vec2(x, pWindow->m_windowSize.y - 1 - y);

            for(const auto& cb : pWindow->m_mouseMoveCallbacks) cb(pos);
            break;
        }

        case WM_MOUSEWHEEL:
        {
            if(io.WantCaptureMouse) break;

            float deltaY = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            glm::vec2 offset = glm::vec2(0, deltaY);

            for(const auto& cb : pWindow->m_scrollCallbacks) cb(offset);
            break;
        }
        case WM_SIZE:
            if(pWindow->gpuState.get_device() != nullptr && wParam != SIZE_MINIMIZED) {
                pWindow->gpuState.waitForLastSubmittedFrame();
                pWindow->gpuState.cleanupRenderTarget();
                HRESULT result = pWindow->gpuState.get_swap_chain()->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
                assert(SUCCEEDED(result) && "Failed to resize swapchain.");
                pWindow->gpuState.createRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if((wParam & 0xfff0) == SC_KEYMENU) return 0; // Disable ALT application menu
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}