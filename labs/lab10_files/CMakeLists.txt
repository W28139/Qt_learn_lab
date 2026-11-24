cmake_minimum_required(VERSION 3.21)

project(lab10_files LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Widgets)

qt_standard_project_setup()

# 保留控制台输出（日志同时写 stderr 和文件）。Windows 上
# qt_add_executable() 默认是 GUI 子系统，这里改回 FALSE。
qt_add_executable(lab10_files
    main.cpp
    mainwindow.cpp
    mainwindow.h
    apppaths.cpp
    apppaths.h
    appsettings.cpp
    appsettings.h
    logging.cpp
    logging.h
    csvexportworker.cpp
    csvexportworker.h
    settingsdialog.cpp
    settingsdialog.h
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

target_link_libraries(lab10_files PRIVATE Qt6::Core Qt6::Network Qt6::Sql Qt6::Widgets)

set_target_properties(lab10_files PROPERTIES WIN32_EXECUTABLE FALSE)

if(MSVC)
    target_compile_options(lab10_files PRIVATE /utf-8)
else()
    target_compile_options(lab10_files PRIVATE -Wall -Wextra)
endif()