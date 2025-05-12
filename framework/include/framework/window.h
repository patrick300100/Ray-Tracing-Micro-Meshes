#pragma once
#include <d3d12.h>
#include "GPUState.h"
#include <imgui/imgui.h>

#include "disable_all_warnings.h"
#include "opengl_includes.h"
// Suppress warnings in third-party code.
DISABLE_WARNINGS_PUSH()
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()
#include <functional>
#include <optional>
#include <string_view>
#include <vector>
#include <filesystem>

class Window {
public:
	Window(std::string_view title, const glm::ivec2& windowSize);
	~Window();

	[[nodiscard]] bool shouldClose() const; // Whether window should close (user clicked the close button).

	void updateInput();
	void swapBuffers(); // Swap the front/back buffer

	void renderToImage(const std::filesystem::path& filePath, const bool flipY = false); // renders the output to an image

	using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
	void registerKeyCallback(KeyCallback&&);
	using CharCallback = std::function<void(unsigned unicodeCodePoint)>;
	void registerCharCallback(CharCallback&&);
	using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
	void registerMouseButtonCallback(MouseButtonCallback&&);
	using MouseMoveCallback = std::function<void(const glm::vec2& cursorPos)>;
	void registerMouseMoveCallback(MouseMoveCallback&&);
	using ScrollCallback = std::function<void(const glm::vec2& offset)>;
	void registerScrollCallback(ScrollCallback&&);
	using WindowResizeCallback = std::function<void(const glm::ivec2& size)>;
	void registerWindowResizeCallback(WindowResizeCallback&&);

	[[nodiscard]] bool isKeyPressed(int key) const;
	[[nodiscard]] bool isMouseButtonPressed(int button) const;

	// NOTE: coordinates are such that the origin is at the left bottom of the screen.
	[[nodiscard]] glm::vec2 getCursorPos() const; // DPI independent.
	[[nodiscard]] glm::vec2 getNormalizedCursorPos() const; // Ranges from 0 to 1.

	[[nodiscard]] glm::ivec2 getWindowSize() const;
	[[nodiscard]] float getAspectRatio() const;
	[[nodiscard]] float getDpiScalingFactor() const;

private:
	WNDCLASSEXW wc;
	HWND hwnd;
	bool done = false;
	ImVec4 clear_color = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
	float m_dpiScalingFactor = 1.0f;

	static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	static std::wstring convertToWString(std::string_view str);

public:
	glm::ivec2 m_windowSize;
	GPUState gpuState;

	std::vector<KeyCallback> m_keyCallbacks;
	std::vector<CharCallback> m_charCallbacks;
	std::vector<MouseButtonCallback> m_mouseButtonCallbacks;
	std::vector<ScrollCallback> m_scrollCallbacks;
	std::vector<MouseMoveCallback> m_mouseMoveCallbacks;
	std::vector<WindowResizeCallback> m_windowResizeCallbacks;
};
