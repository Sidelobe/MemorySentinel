cmake_minimum_required(VERSION 3.9)

# MEMORYSENTINEL

###########################################################
# MAIN
project(MemorySentinel)

# Set C++ standard to C++14
set(CMAKE_CXX_STANDARD 14)
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SOURCE
file(GLOB_RECURSE source "source/*.h*" "source/*.c*")
set (LIB_NAME "${CMAKE_PROJECT_NAME}")

# TEST TARGET
set (TEST_NAME "${LIB_NAME}Test")
file(GLOB_RECURSE source_test "test/*.h*" "test/*.c*")
add_executable(${TEST_NAME} ${source} ${source_test})

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

target_include_directories(${TEST_NAME} PRIVATE source)
target_include_directories(${TEST_NAME} PRIVATE test)
target_include_directories(${TEST_NAME} PRIVATE test/external-utils)

target_link_libraries(${TEST_NAME} dl)
