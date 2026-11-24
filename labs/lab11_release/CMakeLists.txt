# 最低 CMake 版本要求：3.21 起对 Qt6 部署脚本支持较完善
cmake_minimum_required(VERSION 3.21)

# 项目声明：名称、版本号（用于 LAB11_VERSION 编译宏）、语言仅 C++
project(lab11_release VERSION 1.0.0 LANGUAGES CXX)

# 强制使用 C++20 标准，且禁用编译器扩展（保证跨编译器一致性）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找 Qt6 必要模块：Core（非 GUI 基础）、Widgets（桌面 UI）、Test（单元测试）
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)

# Qt 官方推荐的项目初始化：自动设置 AUTOMOC/AUTOUIC/AUTORCC 等
qt_standard_project_setup()

# —— 纯逻辑核心静态库 ——
# 不依赖界面，便于应用与测试同时链接，实现逻辑与 UI 解耦
add_library(lab11_core STATIC
    src/sessionstats.cpp
    src/sessionstats.h
    src/csvutils.cpp
    src/csvutils.h
    src/telemetrytypes.h
)
# PUBLIC 让链接本库的目标自动获得 src/ 头文件搜索路径
target_include_directories(lab11_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# PUBLIC 让下游目标自动链接 Qt6::Core（用到 QByteArray/QString 等）
target_link_libraries(lab11_core PUBLIC Qt6::Core)

# —— 可执行程序 ——
# qt_add_executable 是 Qt 专用封装，自动处理 Windows 上的 WIN32 子系统等平台差异
qt_add_executable(lab11_release
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
)
# 仅本程序使用的链接依赖（Widgets 与核心库）
target_link_libraries(lab11_release PRIVATE Qt6::Core Qt6::Widgets lab11_core)
# 把 CMake 中的工程版本注入代码，供程序运行时展示
target_compile_definitions(lab11_release PRIVATE LAB11_VERSION="${PROJECT_VERSION}")

# —— 编译器警告与编码设置 ——
# Windows 上保持 GUI 子系统不弹控制台；测试程序自行改为控制台（见 tests/CMakeLists.txt）
if(MSVC)
    # MSVC 显式指定源文件为 UTF-8，避免中文注释/字符串乱码
    target_compile_options(lab11_core PRIVATE /utf-8)
    target_compile_options(lab11_release PRIVATE /utf-8)
else()
    # GCC/Clang 开启常用警告，提升代码质量
    target_compile_options(lab11_core PRIVATE -Wall -Wextra)
    target_compile_options(lab11_release PRIVATE -Wall -Wextra)
endif()

# 启用 CTest 测试框架，并加载 tests 子目录（其中用 add_test 注册用例）
enable_testing()
add_subdirectory(tests)

# —— 安装规则 ——
# 将可执行文件安装到安装根目录（macOS 的 .app 为 BUNDLE，Windows/Linux 为 RUNTIME）
install(TARGETS lab11_release
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .)

# Qt 安装期部署：在 install 阶段调用 windeployqt / macdeployqt，
# 自动复制所需 Qt DLL、平台插件等到安装目录。
# Qt 6.4 使用 FILENAME_VARIABLE 输出脚本变量名；6.5+ 改为 OUTPUT_SCRIPT，
# 这里按版本分支兼容两种写法。
if(Qt6_VERSION VERSION_LESS 6.5)
    qt_generate_deploy_app_script(
        TARGET lab11_release
        FILENAME_VARIABLE deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
else()
    qt_generate_deploy_app_script(
        TARGET lab11_release
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
endif()
# 把生成的部署脚本挂到安装流程，执行 install 时自动跑
install(SCRIPT ${deploy_script})