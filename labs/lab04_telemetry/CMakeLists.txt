cmake_minimum_required(VERSION 3.21)

project(lab04_telemetry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(lab04_telemetry
    main.cpp
    telemetrymodel.cpp
    telemetrymodel.h
    ringbuffer.h
    telemetrytypes.h
)

target_link_libraries(lab04_telemetry PRIVATE Qt6::Widgets)

if(MSVC)
    target_compile_options(lab04_telemetry PRIVATE /utf-8)
else()
    target_compile_options(lab04_telemetry PRIVATE -Wall -Wextra)
endif()

set_target_properties(lab04_telemetry PROPERTIES
    WIN32_EXECUTABLE TRUE
)