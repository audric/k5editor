cmake_minimum_required(VERSION 3.21)
set(K5_VERSION "0.1.0" CACHE STRING "Version of this build (release builds pass the tag)")
project(k5editor VERSION ${K5_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)
include(GNUInstallDirs)

add_library(k5core STATIC
  src/core/protocol.cpp src/core/protocol.h
  src/core/f4hwn.cpp src/core/f4hwn.h src/core/defaults.inc
  src/core/presets.cpp src/core/presets.h
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
target_compile_definitions(k5editor PRIVATE
  K5_LISTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lists" K5_LISTS_INSTALL_DIR="${CMAKE_INSTALL_DATADIR}/k5editor/lists")

enable_testing()
add_executable(test_core tests/test_core.cpp tests/fakeradio.h)
target_link_libraries(test_core PRIVATE k5core Qt6::Test)
target_compile_definitions(test_core PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
  K5_LISTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lists")
add_test(NAME core COMMAND test_core)

# ---- install and packages (cpack -G "DEB;TGZ")
install(TARGETS k5editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/k5editor.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(DIRECTORY lists/ DESTINATION ${CMAKE_INSTALL_DATADIR}/k5editor/lists)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})

find_program(DPKG dpkg)
if(DPKG)
  execute_process(COMMAND ${DPKG} --print-architecture OUTPUT_VARIABLE K5_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(K5_ARCH ${CMAKE_SYSTEM_PROCESSOR})
endif()
set(CPACK_PACKAGE_NAME k5editor)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Channel and settings editor for the Quansheng UV-K5 on F4HWN firmware")
set(CPACK_PACKAGE_CONTACT "k5editor maintainers <https://github.com/audric/k5editor>")
set(CPACK_PACKAGE_FILE_NAME "k5editor-${PROJECT_VERSION}-linux-${K5_ARCH}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION hamradio)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/audric/k5editor")
include(CPack)
