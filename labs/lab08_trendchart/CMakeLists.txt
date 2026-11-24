cmake_minimum_required(VERSION 3.21)

project(lab08_trendchart LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

qt_standard_project_setup()

# 保留控制台输出（worker/绘图日志），Windows 上要在下面显式把
# qt_add_executable() 默认的 WIN32_EXECUTABLE 改回 FALSE。
qt_add_executable(lab08_trendchart
    main.cpp
    mainwindow.cpp
    mainwindow.h
    chartwidget.cpp
    chartwidget.h
    boundedqueue.cpp
    boundedqueue.h
    mocktelemetryserver.cpp
    mocktelemetryserver.h
    telemetryworker.cpp
    telemetryworker.h
    ringbuffer.h
    streamdecoder.cpp
    streamdecoder.h
    protocol.cpp
    protocol.h
    crc32.cpp
    crc32.h
    telemetrytypes.h
)

target_link_libraries(lab08_trendchart PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)

set_target_properties(lab08_trendchart PROPERTIES WIN32_EXECUTABLE FALSE)

if(MSVC)
    target_compile_options(lab08_trendchart PRIVATE /utf-8)
else()
    target_compile_options(lab08_trendchart PRIVATE -Wall -Wextra)
endif()