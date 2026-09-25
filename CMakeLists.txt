cmake_minimum_required(VERSION 3.25)
project(HydrogenOS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 ShaderTools)

qt_standard_project_setup(REQUIRES 6.8)

enable_testing()

add_subdirectory(design)
add_subdirectory(shell)
add_subdirectory(settings)
