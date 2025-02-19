cmake_minimum_required(VERSION 3.20)

#  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬  
#  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │  
#  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
    
###########################################################
# MAIN
project(MemorySentinel)

# Set C++ standard to C++14
set(CMAKE_CXX_STANDARD 14)
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CODE_COVERAGE OFF CACHE BOOL "Build with instrumentation and code coverage")


# LIB SOURCES
file(GLOB_RECURSE source "source/*.[h,c]*")
set (LIB_NAME "MemorySentinel")
add_library(${LIB_NAME} ${source})

# TEST TARGET
set (TEST_NAME "${LIB_NAME}Test")
file(GLOB_RECURSE source_test "test/*.[h,c]*")
add_executable(${TEST_NAME} ${source_test})

# Create XCode / VS groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${source})

target_include_directories(${TEST_NAME} PUBLIC source)
target_include_directories(${TEST_NAME} PRIVATE test)
target_include_directories(${TEST_NAME} PRIVATE test/external-utils)

if (CODE_COVERAGE)
  message("Code Coverage tracking enabled")
  # When building with coverage, we usually disable exceptions, for more meaningful results
  # In this case, however, exceptions constitute a fundamental behaviour, so it's best to enable them
  target_compile_options(${LIB_NAME} PRIVATE
          $<$<COMPILE_LANGUAGE:CXX>:-fprofile-arcs -ftest-coverage -fexceptions -fno-inline>
          $<$<COMPILE_LANGUAGE:C>:-fprofile-arcs -ftest-coverage -fexceptions -fno-inline>)
  target_link_options(${LIB_NAME} PRIVATE -fprofile-arcs -ftest-coverage)
  target_compile_options(${TEST_NAME} PRIVATE
          $<$<COMPILE_LANGUAGE:CXX>:-fprofile-arcs -ftest-coverage -fexceptions -fno-inline>
          $<$<COMPILE_LANGUAGE:C>:-fprofile-arcs -ftest-coverage -fexceptions -fno-inline>)
  target_link_options(${TEST_NAME} PRIVATE -fprofile-arcs -ftest-coverage)
endif()

target_link_libraries(${TEST_NAME} ${LIB_NAME})
if ( CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU" )
    target_link_libraries(${TEST_NAME} dl)
endif()

# Explicitly set CMP0110 to "NEW" to allow whitespace in tests names
if (POLICY CMP0110)
  cmake_policy(SET CMP0110 NEW)
endif()

## ENABLE THE USE OF CTEST 
include("test/external-utils/catch2/ParseAndAddCatchTests.cmake")
#include(CTest) # this will generate lots of additional targets
enable_testing()
ParseAndAddCatchTests(${TEST_NAME})
