cmake_minimum_required(VERSION 3.21)
project(k5editor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)

add_library(k5core STATIC
  src/core/protocol.cpp src/core/protocol.h
  src/core/f4hwn.cpp src/core/f4hwn.h
  src/core/sync.cpp src/core/sync.h
  src/core/serialtransport.cpp src/core/serialtransport.h)
target_include_directories(k5core PUBLIC src/core)
target_link_libraries(k5core PUBLIC Qt6::Core)

add_executable(k5editor
  src/main.cpp
  src/gui/channelmodel.cpp src/gui/channelmodel.h
  src/gui/settingspanel.cpp src/gui/settingspanel.h
  src/gui/mainwindow.cpp src/gui/mainwindow.h)
target_link_libraries(k5editor PRIVATE k5core Qt6::Widgets)

enable_testing()
add_executable(test_core tests/test_core.cpp tests/fakeradio.h)
target_link_libraries(test_core PRIVATE k5core Qt6::Test)
target_compile_definitions(test_core PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME core COMMAND test_core)
