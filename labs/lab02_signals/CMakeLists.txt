cmake_minimum_required(VERSION 3.21)

project(lab02_signals LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(lab02_signals
    main.cpp
)

target_link_libraries(lab02_signals PRIVATE Qt6::Widgets)

if(MSVC)
    target_compile_options(lab02_signals PRIVATE /utf-8)
endif()

set_target_properties(lab02_signals PROPERTIES
    WIN32_EXECUTABLE TRUE
)