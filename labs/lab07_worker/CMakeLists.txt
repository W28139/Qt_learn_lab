cmake_minimum_required(VERSION 3.21)

project(lab07_worker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

qt_standard_project_setup()

# 注意：保留控制台输出（worker 日志），必须在下面显式把
# qt_add_executable() 默认的 WIN32_EXECUTABLE 改回 FALSE。
qt_add_executable(lab07_worker
    main.cpp
    boundedqueue.cpp
    boundedqueue.h
    mocktelemetryserver.cpp
    mocktelemetryserver.h
    telemetryworker.cpp
    telemetryworker.h
    mainwindow.cpp
    mainwindow.h
    telemetrymodel.cpp
    telemetrymodel.h
    ringbuffer.h
    streamdecoder.cpp
    streamdecoder.h
    protocol.cpp
    protocol.h
    crc32.cpp
    crc32.h
    telemetrytypes.h
)

target_link_libraries(lab07_worker PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)

# qt_add_executable() 在 Windows 上默认把程序设为 GUI 子系统（无控制台），
# 这里显式改回控制台程序，否则 worker 日志在 cmd/Qt Creator 里看不到。
set_target_properties(lab07_worker PROPERTIES WIN32_EXECUTABLE FALSE)

if(MSVC)
    target_compile_options(lab07_worker PRIVATE /utf-8)
else()
    target_compile_options(lab07_worker PRIVATE -Wall -Wextra)
endif()