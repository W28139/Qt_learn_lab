# 需要 Qt6 的 qt_standard_project_setup() 等 CMake 接口
cmake_minimum_required(VERSION 3.21)

project(lab05_codec LANGUAGES CXX)

# 代码用到 std::bit_cast（C++20）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 协议层只依赖 Qt Core：不需要 Widgets/Network，便于控制台运行和复用
find_package(Qt6 REQUIRED COMPONENTS Core)

qt_standard_project_setup()

# 源文件显式列出；头文件也列出来，方便在 Qt Creator 工程树里看到
qt_add_executable(lab05_codec
    main.cpp
    protocol.cpp
    protocol.h
    crc32.cpp
    crc32.h
    telemetrytypes.h
)

# 只链接 Qt6::Core，防止误用 GUI/网络模块
target_link_libraries(lab05_codec PRIVATE Qt6::Core)

# qt_add_executable() 在 Windows 上默认把程序设为 GUI 子系统（无控制台）。
# 本 Lab 是控制台程序，必须显式关掉，否则 Windows 上看不到任何输出。
set_target_properties(lab05_codec PROPERTIES WIN32_EXECUTABLE FALSE)

# MSVC 默认按本地代码页读源码，含中文的源文件会乱码甚至编译失败，
# 必须加 /utf-8；其他编译器打开常见警告，帮助发现符号/截断问题
if(MSVC)
    target_compile_options(lab05_codec PRIVATE /utf-8)
else()
    target_compile_options(lab05_codec PRIVATE -Wall -Wextra)
endif()