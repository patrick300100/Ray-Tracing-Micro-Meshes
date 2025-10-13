#include "window.h"
#include <imgui/imgui.h>
#undef IMGUI_IMPL_OPENGL_LOADER_GLEW
#define IMGUI_IMPL_OPENGL_LOADER_GLAD 1
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

Window::Window(std::string_view title, const glm::ivec2& wSize, GPUState* gpuStatePtr): windowSize(wSize) {
    // std::string_view does not guarantee that the string contains a terminator character.
    const auto titleString = convertToWString(title);

    // Create application window
    wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"MicroMeshWindow", nullptr };
    RegisterClassExW(&wc);
    hwnd = CreateWindowW(wc.lpszClassName, titleString.c_str(), WS_OVERLAPPEDWINDOW, 100, 100, wSize.x, wSize.y, nullptr, nullptr, wc.hInstance, nullptr);

    // Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    auto* data = new WindowData{this, gpuStatePtr};
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
}

Window::~Window() {
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
    while(PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if(msg.message == WM_QUIT) done = true;
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
    ScreenToClient(hwnd, &p);
    return glm::vec2(static_cast<float>(p.x), static_cast<float>(windowSize.y - 1 - p.y));
}

glm::vec2 Window::getNormalizedCursorPos() const {
    return getCursorPos() / glm::vec2(windowSize);
}

glm::ivec2 Window::getWindowSize() const {
    return windowSize;
}

float Window::getAspectRatio() const {
    if(windowSize.x == 0 || windowSize.y == 0) return 1.0f;
    return float(windowSize.x) / float(windowSize.y);
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
    auto* windowData = reinterpret_cast<WindowData*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if(!windowData) return DefWindowProcW(hWnd, msg, wParam, lParam);

    switch(msg) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            int action = (msg == WM_KEYDOWN) ? GLFW_PRESS : GLFW_RELEASE;
            int key = static_cast<int>(wParam);
            int scancode = 0; // Optional: can use MapVirtualKey if needed
            int mods = 0;     // Optional: get with GetKeyState(VK_SHIFT), etc.

            for(const auto& cb : windowData->window->m_keyCallbacks) cb(key, scancode, action, mods);
            break;
        }

        case WM_CHAR:
        {
            auto codepoint = static_cast<unsigned>(wParam);
            for(const auto& cb : windowData->window->m_charCallbacks) cb(codepoint);
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        {
            int button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) ? GLFW_MOUSE_BUTTON_LEFT : GLFW_MOUSE_BUTTON_RIGHT;
            int action = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) ? GLFW_PRESS : GLFW_RELEASE;
            int mods = 0; // Optional modifier keys

            for(const auto& cb : windowData->window->m_mouseButtonCallbacks) cb(button, action, mods);
            break;
        }

        case WM_MOUSEMOVE:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            glm::vec2 pos = glm::vec2(x, windowData->window->windowSize.y - 1 - y);

            for(const auto& cb : windowData->window->m_mouseMoveCallbacks) cb(pos);
            break;
        }

        case WM_MOUSEWHEEL:
        {
            float deltaY = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            glm::vec2 offset = glm::vec2(0, deltaY);

            for(const auto& cb : windowData->window->m_scrollCallbacks) cb(offset);
            break;
        }
        case WM_SIZE:
            if(wParam != SIZE_MINIMIZED) {
                windowData->gpuState->waitForLastSubmittedFrame();
                windowData->gpuState->cleanupRenderTarget();
                HRESULT result = windowData->gpuState->get_swap_chain()->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
                assert(SUCCEEDED(result) && "Failed to resize swapchain.");
                windowData->gpuState->createRenderTarget();
            }
            return 0;
        case WM_NCDESTROY:
            delete reinterpret_cast<WindowData*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
            break;
        case WM_SYSCOMMAND:
            if((wParam & 0xfff0) == SC_KEYMENU) return 0; // Disable ALT application menu
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

HWND Window::getHWND() const {
    return hwnd;
}

WNDCLASSEXW Window::getWc() const {
    return wc;
}

glm::uvec2 Window::getRenderDimension() const {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int w = clientRect.right  - clientRect.left;
    int h = clientRect.bottom - clientRect.top;

    return {w, h};
}
