cmake_minimum_required(VERSION 3.16)
project(deskpet CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DESKPET_SIM "Build the SDL2 desktop simulator" ON)
option(DESKPET_SANITIZE "Build with address/undefined sanitizers" OFF)

include(FetchContent)
FetchContent_Declare(ArduinoJson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(ArduinoJson)
if(NOT arduinojson_POPULATED)
  FetchContent_Populate(ArduinoJson)
endif()

if(DESKPET_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# --- Portable engine (the same sources PlatformIO builds for the board) ---
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS engine/src/*.cpp)
add_library(deskpet_engine STATIC ${ENGINE_SOURCES})
target_include_directories(deskpet_engine PUBLIC engine/include ${arduinojson_SOURCE_DIR}/src)
target_compile_definitions(deskpet_engine PUBLIC ARDUINOJSON_USE_LONG_LONG=1)
target_compile_options(deskpet_engine PRIVATE -Wall -Wextra)

# --- PC HAL implementations ---
add_library(deskpet_pc STATIC platform/pc/pc_platform.cpp platform/pc/http_portal.cpp)
target_include_directories(deskpet_pc PUBLIC platform/pc)
target_link_libraries(deskpet_pc PUBLIC deskpet_engine)

# --- Headless runner: scripted scenario + screenshots, no window needed ---
add_executable(deskpet_headless platform/pc/main_headless.cpp)
target_link_libraries(deskpet_headless PRIVATE deskpet_pc)

# --- Unit tests ---
enable_testing()
add_executable(deskpet_tests tests/test_main.cpp)
target_link_libraries(deskpet_tests PRIVATE deskpet_pc)
target_compile_definitions(deskpet_tests PRIVATE DESKPET_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME deskpet_tests COMMAND deskpet_tests)

# --- SDL2 simulator ---
if(DESKPET_SIM)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    add_executable(deskpet_sim platform/sim/main_sdl.cpp)
    target_include_directories(deskpet_sim PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(deskpet_sim PRIVATE deskpet_pc ${SDL2_LIBRARIES})
  else()
    message(STATUS "SDL2 not found: skipping deskpet_sim (headless runner and tests still build)")
  endif()
endif()
