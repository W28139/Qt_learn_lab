# 要求 CMake 最低版本为 3.21
cmake_minimum_required(VERSION 3.21)

# 定义项目名为 lab01_timer，项目使用的语言是 C++（CXX）
project(lab01_timer LANGUAGES CXX)

# 设置 C++ 标准为 C++20；
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 编译器必须支持 C++20，否则失败
# set(CMAKE_CXX_EXTENSIONS OFF) # 表示不使用编译器私有扩展,用标准 -std=c++20

# 查找 Qt6，并且必须找到 Widgets 模块（传统桌面 GUI 控件模块）
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# 使用 Qt 提供的便捷命令
qt_standard_project_setup()
# 用 Qt 提供的命令创建一个可执行目标 lab01_timer，源文件有 main.cpp
qt_add_executable(lab01_timer
    main.cpp
)

# 把 lab01_timer 链接到 Qt6 的 Widgets 库
target_link_libraries(lab01_timer PRIVATE Qt6::Widgets)

# if(MSVC)
#     target_compile_options(lab01_timer PRIVATE /utf-8)
# endif()

# 设置目标属性：在 Windows 上生成 GUI 程序而不是控制台程序。
# set_target_properties(lab01_timer PROPERTIES
#     WIN32_EXECUTABLE TRUE
# )