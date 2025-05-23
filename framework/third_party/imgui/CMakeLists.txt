add_library(imgui
	"src/imgui.cpp"
	"src/imgui_demo.cpp"
	"src/imgui_draw.cpp"
	"src/imgui_tables.cpp"
	"src/imgui_widgets.cpp"
	"src/imgui_impl_dx12.cpp"
	"src/imgui_impl_win32.cpp"
)
target_include_directories(imgui PUBLIC "include" PRIVATE "include/imgui")