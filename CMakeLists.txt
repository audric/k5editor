cmake_minimum_required(VERSION 3.21)
set(K5_VERSION "0.3.0" CACHE STRING "Version of this build (release builds pass the tag)")
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

# Linux packages follow the usual bin/ and share/ layout. The Windows package is one
# folder holding the program, the Qt libraries and the lists.
if(WIN32)
  set(K5_SERIAL_SOURCE src/core/serialtransport_win.cpp)
  set(K5_BIN_DIR .)
  set(K5_LISTS_DIR lists)
  set(K5_DOC_DIR .)
  set(K5_LISTS_FROM_BINARY lists)
else()
  set(K5_SERIAL_SOURCE src/core/serialtransport.cpp)
  set(K5_BIN_DIR ${CMAKE_INSTALL_BINDIR})
  set(K5_LISTS_DIR ${CMAKE_INSTALL_DATADIR}/k5editor/lists)
  set(K5_DOC_DIR ${CMAKE_INSTALL_DOCDIR})
  set(K5_LISTS_FROM_BINARY ../${K5_LISTS_DIR})
endif()

add_library(k5core STATIC
  src/core/protocol.cpp src/core/protocol.h
  src/core/f4hwn.cpp src/core/f4hwn.h src/core/defaults.inc
  src/core/presets.cpp src/core/presets.h
  src/core/sync.cpp src/core/sync.h
  ${K5_SERIAL_SOURCE} src/core/serialtransport.h)
target_include_directories(k5core PUBLIC src/core)
target_link_libraries(k5core PUBLIC Qt6::Core)

add_executable(k5editor WIN32
  src/main.cpp
  src/gui/channelmodel.cpp src/gui/channelmodel.h
  src/gui/settingspanel.cpp src/gui/settingspanel.h
  src/gui/mainwindow.cpp src/gui/mainwindow.h)
target_link_libraries(k5editor PRIVATE k5core Qt6::Widgets)
target_compile_definitions(k5editor PRIVATE
  K5_LISTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lists" K5_LISTS_FROM_BINARY="${K5_LISTS_FROM_BINARY}")

enable_testing()
add_executable(test_core tests/test_core.cpp tests/fakeradio.h)
target_link_libraries(test_core PRIVATE k5core Qt6::Test)
target_compile_definitions(test_core PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
  K5_LISTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lists")
add_test(NAME core COMMAND test_core)

# ---- install and packages (cpack -G "DEB;TGZ", or cpack -G ZIP on Windows)
install(TARGETS k5editor RUNTIME DESTINATION ${K5_BIN_DIR})
install(DIRECTORY lists/ DESTINATION ${K5_LISTS_DIR})
install(FILES README.md LICENSE DESTINATION ${K5_DOC_DIR})

if(WIN32)
  # Copy the Qt and compiler libraries the program needs next to it.
  get_target_property(K5_QMAKE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(K5_QT_BIN "${K5_QMAKE}" DIRECTORY)
  find_program(WINDEPLOYQT windeployqt HINTS "${K5_QT_BIN}" REQUIRED)
  install(CODE "execute_process(COMMAND \"${WINDEPLOYQT}\" --release --no-translations --no-opengl-sw --compiler-runtime \"\${CMAKE_INSTALL_PREFIX}/k5editor.exe\" COMMAND_ERROR_IS_FATAL ANY)")
  set(K5_PACKAGE_SYSTEM windows-x64)
else()
  install(FILES packaging/k5editor.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  find_program(DPKG dpkg)
  if(DPKG)
    execute_process(COMMAND ${DPKG} --print-architecture OUTPUT_VARIABLE K5_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
  else()
    set(K5_ARCH ${CMAKE_SYSTEM_PROCESSOR})
  endif()
  set(K5_PACKAGE_SYSTEM linux-${K5_ARCH})
endif()
set(CPACK_PACKAGE_NAME k5editor)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Channel and settings editor for the Quansheng UV-K5 on F4HWN firmware")
set(CPACK_PACKAGE_CONTACT "k5editor maintainers <https://github.com/audric/k5editor>")
set(CPACK_PACKAGE_FILE_NAME "k5editor-${PROJECT_VERSION}-${K5_PACKAGE_SYSTEM}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION hamradio)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/audric/k5editor")
include(CPack)
