add_library(tinygltf "src/tiny_gltf.cc")
target_include_directories(tinygltf PUBLIC "include" PRIVATE "include/tinygltf")
target_compile_definitions(tinygltf INTERFACE -DTINYGLTF_USE_CPP14) # Use C++14 if needed
