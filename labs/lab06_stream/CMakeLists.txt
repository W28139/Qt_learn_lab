cmake_minimum_required(VERSION 3.21)

project(lab06_stream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_standard_project_setup()

qt_add_executable(lab06_stream
    main.cpp
    streamdecoder.cpp
    streamdecoder.h
    server.cpp
    server.h
    client.cpp
    client.h
    protocol.cpp
    protocol.h
    crc32.cpp
    crc32.h
    telemetrytypes.h
)

target_link_libraries(lab06_stream PRIVATE Qt6::Core Qt6::Network)

# qt_add_executable() 在 Windows 上默认把程序设为 GUI 子系统（无控制台）。
# 本 Lab 是控制台程序（--server / --client 都要打印日志），
# 必须显式关掉，否则 Windows 上这些输出看不到。
set_target_properties(lab06_stream PROPERTIES WIN32_EXECUTABLE FALSE)

if(MSVC)
    target_compile_options(lab06_stream PRIVATE /utf-8)
else()
    target_compile_options(lab06_stream PRIVATE -Wall -Wextra)
endif()