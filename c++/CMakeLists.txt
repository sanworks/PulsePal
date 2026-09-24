# Builds the Pulse Pal C++ class, an example program, and the offline protocol tests. See README.md.
#
# The class uses libserialport (https://sigrok.org/wiki/Libserialport) for the USB serial port:
#   Windows: downloaded and built with this project, unless PULSEPAL_BUNDLE_LIBSERIALPORT is OFF
#   macOS:   brew install libserialport pkg-config
#   Linux:   sudo apt install libserialport-dev pkg-config

cmake_minimum_required(VERSION 3.14)
project(PulsePal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  set(PULSEPAL_BUNDLE_LIBSERIALPORT_DEFAULT ON)
else()
  set(PULSEPAL_BUNDLE_LIBSERIALPORT_DEFAULT OFF)
endif()
option(PULSEPAL_BUNDLE_LIBSERIALPORT "Download libserialport and build it with Pulse Pal (Windows only)"
       ${PULSEPAL_BUNDLE_LIBSERIALPORT_DEFAULT})
option(PULSEPAL_BUILD_TESTS "Build the offline protocol tests" ON)

if(PULSEPAL_BUNDLE_LIBSERIALPORT)
  if(NOT WIN32)
    message(FATAL_ERROR "PULSEPAL_BUNDLE_LIBSERIALPORT is for Windows. On macOS and Linux, install libserialport "
                        "with your package manager (see README.md).")
  endif()
  # libserialport's release tarball, pinned by hash. On Windows it needs no configure step: its own Visual Studio
  # project compiles these three files with LIBSERIALPORT_MSBUILD defined, and links setupapi.
  include(FetchContent)
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW) # Give extracted files the time of extraction, so that a new URL rebuilds them
  endif()
  FetchContent_Declare(libserialport
    URL https://sigrok.org/download/source/libserialport/libserialport-0.1.2.tar.gz
    URL_HASH SHA256=5deb92b5ca72c0347b07b786848350deca2dcfd975ce613b8e0e1d947a4b4ca9
  )
  FetchContent_MakeAvailable(libserialport) # No CMakeLists.txt in the tarball, so this only downloads it
  add_library(serialport STATIC
    ${libserialport_SOURCE_DIR}/serialport.c
    ${libserialport_SOURCE_DIR}/timing.c
    ${libserialport_SOURCE_DIR}/windows.c
  )
  target_include_directories(serialport PUBLIC ${libserialport_SOURCE_DIR})
  # PUBLIC, because with MSVC, libserialport.h declares its functions dllimport unless LIBSERIALPORT_MSBUILD is
  # defined, and dllimport declarations do not link to a static library
  target_compile_definitions(serialport PUBLIC LIBSERIALPORT_MSBUILD)
  target_link_libraries(serialport PRIVATE setupapi)
  set(PULSEPAL_LIBSERIALPORT serialport)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBSERIALPORT REQUIRED IMPORTED_TARGET libserialport)
  set(PULSEPAL_LIBSERIALPORT PkgConfig::LIBSERIALPORT)
endif()

# The Pulse Pal class. To use it in another CMake project: add_subdirectory(path/to/PulsePal/c++), then
# target_link_libraries(your_target PRIVATE pulsepal)
add_library(pulsepal STATIC PulsePal.cpp PulsePal.h SerialPort.cpp SerialPort.h)
target_include_directories(pulsepal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pulsepal PRIVATE ${PULSEPAL_LIBSERIALPORT})
if(MSVC)
  target_compile_options(pulsepal PRIVATE /W4)
else()
  target_compile_options(pulsepal PRIVATE -Wall -Wextra)
endif()

# Example program. Needs a connected Pulse Pal
add_executable(PulsePalExample PulsePalExample.cpp)
target_link_libraries(PulsePalExample PRIVATE pulsepal)

if(PULSEPAL_BUILD_TESTS)
  enable_testing()
  add_executable(test_protocol tests/test_protocol.cpp)
  target_link_libraries(test_protocol PRIVATE pulsepal)
  add_test(NAME protocol COMMAND test_protocol)
endif()
