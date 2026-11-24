# 每个测试是独立的可执行文件；CTest 负责统一调度。
qt_add_executable(tst_sessionstats tst_sessionstats.cpp)
target_link_libraries(tst_sessionstats PRIVATE Qt6::Test lab11_core)
set_target_properties(tst_sessionstats PROPERTIES WIN32_EXECUTABLE FALSE)

qt_add_executable(tst_csvutils tst_csvutils.cpp)
target_link_libraries(tst_csvutils PRIVATE Qt6::Test lab11_core)
set_target_properties(tst_csvutils PROPERTIES WIN32_EXECUTABLE FALSE)

if(MSVC)
    target_compile_options(tst_sessionstats PRIVATE /utf-8)
    target_compile_options(tst_csvutils PRIVATE /utf-8)
else()
    target_compile_options(tst_sessionstats PRIVATE -Wall -Wextra)
    target_compile_options(tst_csvutils PRIVATE -Wall -Wextra)
endif()

add_test(NAME sessionstats COMMAND tst_sessionstats)
add_test(NAME csvutils COMMAND tst_csvutils)

set_tests_properties(sessionstats csvutils PROPERTIES
    TIMEOUT 30
    LABELS "unit")