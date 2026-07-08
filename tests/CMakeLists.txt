cmake_minimum_required(VERSION 3.28)
project(linux-sysmon-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Catch2 comes from parent's FetchContent
add_executable(test_sysmon test_main.cpp)

target_include_directories(test_sysmon PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_sysmon PRIVATE
  Catch2::Catch2WithMain
  spdlog::spdlog
)

# Manual test registration (works with any CMake version)
include(CTest)
add_test(NAME test_sysmon COMMAND test_sysmon)
