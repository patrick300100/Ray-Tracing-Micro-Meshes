include(FetchContent)

set(CMAKE_CXX_FLAGS_COPY ${CMAKE_CXX_FLAGS})
if (MSVC)
	# Disable deprecation warnings when compiling fmtlib with MSVC.
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS")
endif()

add_subdirectory("catch2")
add_subdirectory("glm")
add_subdirectory("fmt")
if (NOT FRAMEWORK_BASIC_LIBRARY)
	add_subdirectory("tinyobjloader")
	add_subdirectory("glad")
	add_subdirectory("glfw3")
	add_subdirectory("imgui")
	add_subdirectory("stb")
	add_subdirectory("nativefiledialog")
	add_subdirectory("toml")
	add_subdirectory("tinygltf")
	add_subdirectory("json")
endif()

FetchContent_Declare(micromesh-tools GIT_REPOSITORY https://github.com/NVlabs/micromesh-tools.git GIT_TAG "f542b31")
FetchContent_MakeAvailable(micromesh-tools)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS_COPY})