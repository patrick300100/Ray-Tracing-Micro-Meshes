cmake_minimum_required(VERSION 3.29 FATAL_ERROR)
project(ComputerGraphics C CXX)

set(CMAKE_CXX_STANDARD 23)

# Only compile on Windows
if(NOT WIN32)
	message(FATAL_ERROR "This project requires Windows (DirectX 12)")
endif()

add_subdirectory("framework")

add_executable(Micro_Meshes
    "src/application.cpp"
	"src/GPUMesh.cpp"
	"src/WireframeDraw.cpp"
)

target_compile_definitions(Micro_Meshes PRIVATE RESOURCE_ROOT="${CMAKE_CURRENT_LIST_DIR}/")
target_link_libraries(Micro_Meshes PRIVATE CGFramework d3d12 dxgi d3dcompiler)
enable_sanitizers(Micro_Meshes)
set_project_warnings(Micro_Meshes)

# We would like to copy the files when they changed. Even if no *.cpp files were modified (and
# thus no build is triggered). We tell CMake that the executable depends on the shader files in
# the build directory. We also tell it how to generate those files (by copying them from the
# shaders folder in this directory). The gather all glsl files in the shaders folder when CMake
# is configured. So if you were to add a shader file then you need to configure CMake again.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders/")

set(Micro_Meshes_shader_copies "")
# List all *.glsl files in the shaders folder
file(GLOB Micro_Meshes_shader_sources "${CMAKE_CURRENT_LIST_DIR}/shaders/*.glsl")
foreach(shader_file IN LISTS Micro_Meshes_shader_sources)
	get_filename_component(file_name ${shader_file} NAME)
	message("shader_file: ${file_name}")
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${file_name}"
		COMMAND ${CMAKE_COMMAND} -E copy
		"${CMAKE_CURRENT_LIST_DIR}/shaders/${file_name}"
		"${CMAKE_CURRENT_BINARY_DIR}/shaders/${file_name}"
		DEPENDS "${CMAKE_CURRENT_LIST_DIR}/shaders/${file_name}"
		)
	LIST(APPEND Micro_Meshes_shader_copies "${CMAKE_CURRENT_BINARY_DIR}/shaders/${file_name}")
endforeach()
add_custom_target(Micro_Meshes_copy_shaders DEPENDS ${Micro_Meshes_shader_copies})
add_dependencies(Micro_Meshes Micro_Meshes_copy_shaders)
