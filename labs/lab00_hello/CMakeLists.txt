cmake_minimum_required(VERSION 3.21)

project(lab00_hello LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 找到 Qt6 的 CMake 配置，并加载 `Widgets` 组件（它会自动带上 `Core` 和 `Gui`）
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Qt 6.3+ 的官方推荐：
# 开启 `AUTOMOC`（自动跑 moc）和 `AUTOUIC`，并设置安装目录、RPATH、输出目录等跨平台默认属性
# （`AUTORCC` 不包含在内，本项目暂时用不到）
qt_standard_project_setup()

# Qt 封装的 `add_executable`，处理 Windows/Mac 的平台差异，并配合 AUTOMOC
qt_add_executable(lab00_hello
    main.cpp
)

# 链接 Qt Widgets；头文件路径、DLL 依赖由 CMake 自动处理
target_link_libraries(lab00_hello PRIVATE Qt6::Widgets)

# MSVC 默认按本地编码解析源文件，中文会乱码；必须显式指定 UTF-8
if(MSVC)
    target_compile_options(lab00_hello PRIVATE /utf-8)
endif()

# 不加会出现一个黑色控制台窗口；加了仍然可以在 Qt Creator 的“应用程序输出”里看到 `qInfo/qDebug`
set_target_properties(lab00_hello PROPERTIES
    WIN32_EXECUTABLE TRUE
)