cmake_minimum_required(VERSION 3.16)
project(hdmi-transcode LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)

# Output side: DRM/KMS display, mode choice, hot-plug + test pattern (R1, R2, R2a, R2b, R4, N6).
add_library(output STATIC
  src/drm_output.cpp
  src/edid.cpp
  src/hotplug.cpp
  src/mode_select.cpp
  src/test_pattern.cpp
)
target_include_directories(output PUBLIC src)
target_link_libraries(output PUBLIC PkgConfig::DRM PkgConfig::UDEV)

add_executable(hdmi-transcode-pattern src/pattern_main.cpp)
target_link_libraries(hdmi-transcode-pattern PRIVATE output)

enable_testing()
add_executable(test_pattern_tests tests/test_pattern_tests.cpp)
target_link_libraries(test_pattern_tests PRIVATE output)
add_test(NAME test_pattern COMMAND test_pattern_tests)

add_executable(mode_select_tests tests/mode_select_tests.cpp)
target_link_libraries(mode_select_tests PRIVATE output)
add_test(NAME mode_select COMMAND mode_select_tests)
