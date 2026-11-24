cmake_minimum_required(VERSION 3.21)

project(lab09_storage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Widgets)

qt_standard_project_setup()

# 保留控制台输出（worker/db 日志）。Windows 上 qt_add_executable() 默认是
# GUI 子系统，这里显式改回 FALSE，否则控制台看不到 qInfo()。
qt_add_executable(lab09_storage
    main.cpp
    mainwindow.cpp
    mainwindow.h
    dbwriter.cpp
    dbwriter.h
    databaseschema.cpp
    databaseschema.h
    livemodel.cpp
    livemodel.h
    historypanel.cpp
    historypanel.h
    boundedqueue.cpp
    boundedqueue.h
    mocktelemetryserver.cpp
    mocktelemetryserver.h
    telemetryworker.cpp
    telemetryworker.h
    streamdecoder.cpp
    streamdecoder.h
    protocol.cpp
    protocol.h
    crc32.cpp
    crc32.h
    telemetrytypes.h
)

target_link_libraries(lab09_storage PRIVATE Qt6::Core Qt6::Network Qt6::Sql Qt6::Widgets)

set_target_properties(lab09_storage PROPERTIES WIN32_EXECUTABLE FALSE)

if(MSVC)
    target_compile_options(lab09_storage PRIVATE /utf-8)
else()
    target_compile_options(lab09_storage PRIVATE -Wall -Wextra)
endif()