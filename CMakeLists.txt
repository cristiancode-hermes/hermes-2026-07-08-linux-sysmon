cmake_minimum_required(VERSION 3.28)
project(linux-sysmon VERSION 1.0.0 LANGUAGES CXX)

# ── C++23 (GCC 14 required for std::expected + std::println) ──
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies (FetchContent — no vcpkg needed) ──
include(FetchContent)

# spdlog (header-only logging)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.1
  GIT_SHALLOW    TRUE
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "")
set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "")
set(SPDLOG_BUILD_BENCH   OFF CACHE BOOL "")
FetchContent_MakeAvailable(spdlog)

# Catch2 (tests only)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.0
    GIT_SHALLOW    TRUE
  )
  set(CATCH_BUILD_TESTING OFF CACHE BOOL "")
  FetchContent_MakeAvailable(Catch2)
endif()

# ── Sources ──
file(GLOB_RECURSE HEADERS include/*.hpp)
file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(linux-sysmon ${SOURCES} ${HEADERS})
target_include_directories(linux-sysmon PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(linux-sysmon PRIVATE
  spdlog::spdlog
)

# Link librt for POSIX shared memory (not needed here but future-proof)
# target_link_libraries(linux-sysmon PRIVATE rt)

# ── Compiler warnings ──
target_compile_options(linux-sysmon PRIVATE
  -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion
  -Wnull-dereference -Wdouble-promotion
  -Wformat=2
  $<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-omit-frame-pointer>
  $<$<CONFIG:Debug>:-O1 -g3>
)

target_link_options(linux-sysmon PRIVATE
  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# ── Tests ──
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ── Install ──
install(TARGETS linux-sysmon DESTINATION bin)
