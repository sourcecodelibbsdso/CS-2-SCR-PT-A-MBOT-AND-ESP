cmake_minimum_required(VERSION 3.20)
project(CS2ModMenu CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGui sources (place imgui/ folder next to CMakeLists.txt)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
)

add_executable(CS2ModMenu WIN32
    src/main.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(CS2ModMenu PRIVATE ${IMGUI_DIR})
target_link_libraries(CS2ModMenu PRIVATE d3d11 dwmapi)
target_compile_options(CS2ModMenu PRIVATE /W3 /O2)
