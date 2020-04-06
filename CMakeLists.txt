cmake_minimum_required(VERSION 3.9)

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

# SOURCE
file(GLOB_RECURSE source "source/*.h*" "source/*.c*")
set (LIB_NAME "MemorySentinel")
add_library(${LIB_NAME} ${source})

# TEST TARGET
set (TEST_NAME "${LIB_NAME}Test")
file(GLOB_RECURSE source_test "test/*.h*" "test/*.c*")
add_executable(${TEST_NAME} ${source_test})

# show groups that match folder structure - in Xcode / VisualStudio
get_target_property(allsources ${TEST_NAME} SOURCES)
foreach(_source IN ITEMS ${allsources})
    if (IS_ABSOLUTE "${_source}")
        file(RELATIVE_PATH _source_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_source}")
    else()
       set(_source_rel "${_source}")
    endif()
    get_filename_component(_source_path "${_source_rel}" PATH)
    string(REPLACE "/" "\\" _source_path_msvc "${_source_path}")
    source_group("${_source_path_msvc}" FILES "${_source}")
endforeach()

target_include_directories(${TEST_NAME} PUBLIC source)
target_include_directories(${TEST_NAME} PRIVATE test)
target_include_directories(${TEST_NAME} PRIVATE test/external-utils)

target_link_libraries(${TEST_NAME} ${LIB_NAME})
target_link_libraries(${TEST_NAME} dl)

## ENABLE THE USE OF CTEST 
include("test/external-utils/catch2/ParseAndAddCatchTests.cmake")
#include(CTest) # this will generate lots of additional targets
enable_testing()
ParseAndAddCatchTests(${TEST_NAME})
