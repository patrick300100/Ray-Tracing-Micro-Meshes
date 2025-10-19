# Ray Tracing Micro-Meshes

This repository implements an approach for ray tracing NVIDIA's micro-meshes. It was developed as part of a master's 
thesis project. The accommodating paper can be found on the 
[TU Delft Repository](https://resolver.tudelft.nl/uuid:c7381401-0f0c-4b2a-a291-8e62d65ae0df).

## How to run
The application accepts 2 command-line arguments. The first one is the path to the micro-mesh file. This should be a 
`*.gltf` file which includes a link to the `*.bary` file. A second optional parameter can be provided, `-T`, which 
specifies whether a tessellated version of the micro-mesh should be ray traced.

Please note that this application was developed under the assumption that the micro-meshes are obtained from the 
application developed by [Maggiordomo et al](https://github.com/NVlabs/micromesh-tools). Micro-meshes generated 
through other methods have not been tested and may therefore not work correctly.

## Building the project
This project uses CMake to manage builds. 
You can generate the necessary project files using CMake, and most IDEs that support CMake will build the project 
automatically when opened.

The ray tracer is implemented with DirectX Raytracing (DXR) and therefore requires the Visual Studio toolchain to build. 
If your IDE defaults to another toolchain (e.g., MinGW), you must switch to a Visual Studio toolchain in your CMake settings.