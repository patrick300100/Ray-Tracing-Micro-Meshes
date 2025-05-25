file(GLOB DX_UTIL_SOURCES
        "*.cpp"
        "*.h"
)

add_library(dx_util STATIC ${DX_UTIL_SOURCES})

target_include_directories(dx_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(dx_util PRIVATE "../../framework/third_party/d3dx12")
