cmake_minimum_required(VERSION 3.16)
project(simulador LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)

add_library(imgui
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/imgui_tables.cpp
)

target_include_directories(imgui PUBLIC external/imgui)

add_library(implot
    external/implot/implot.cpp
    external/implot/implot_items.cpp
)

target_include_directories(implot PUBLIC
    external/implot
    external/imgui
)

target_link_libraries(implot PUBLIC imgui)

add_library(imgui_sfml
    external/imgui-sfml/imgui-SFML.cpp
)

target_include_directories(imgui_sfml PUBLIC
    external/imgui
    external/imgui-sfml
)

target_link_libraries(imgui_sfml PUBLIC imgui sfml-graphics sfml-window sfml-system)

file(GLOB_RECURSE SRC_FILES src/*.cpp)

add_executable(simulador ${SRC_FILES})

target_link_libraries(simulador
    imgui
    implot
    imgui_sfml
    sfml-graphics
    sfml-window
    sfml-system
)

if(APPLE)
    target_link_libraries(simulador "-framework OpenGL" "-framework Cocoa")
endif()
