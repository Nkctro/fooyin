find_package(Ebur128 QUIET)

if(TARGET Ebur128::Ebur128)
    message(STATUS "Using pre-defined libebur128 target")
    return()
endif()

if(Ebur128_FOUND)
    message(STATUS "Using system libebur128")
    return()
endif()

message(STATUS "Using bundled libebur128")

include(FetchContent)

set(EBUR128_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(EBUR128_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EBUR128_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libebur128
    GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
    GIT_TAG v1.2.6
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_GetProperties(libebur128)
if(NOT libebur128_POPULATED)
    FetchContent_Populate(libebur128)

    set(libebur128_cmake_file "${libebur128_SOURCE_DIR}/CMakeLists.txt")
    foreach(_libebur128_patch_file
            "${libebur128_SOURCE_DIR}/CMakeLists.txt"
            "${libebur128_SOURCE_DIR}/test/CMakeLists.txt")
        if(EXISTS "${_libebur128_patch_file}")
            file(READ "${_libebur128_patch_file}" _libebur128_cmake_contents)
            string(REGEX REPLACE "cmake_minimum_required\\s*\\(VERSION[^\\)]*\\)" "cmake_minimum_required(VERSION 3.16)" _libebur128_cmake_contents "${_libebur128_cmake_contents}")
            file(WRITE "${_libebur128_patch_file}" "${_libebur128_cmake_contents}")
        endif()
    endforeach()

    set(_libebur128_prev_build_shared_libs "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${libebur128_SOURCE_DIR}" "${libebur128_BINARY_DIR}" EXCLUDE_FROM_ALL)
    if(_libebur128_prev_build_shared_libs)
        set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    else()
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    endif()
endif()

if(TARGET ebur128)
    target_include_directories(ebur128 PUBLIC "${libebur128_SOURCE_DIR}/ebur128")
    if(NOT TARGET Ebur128::Ebur128)
        add_library(Ebur128::Ebur128 INTERFACE IMPORTED)
        set_target_properties(
            Ebur128::Ebur128
            PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${libebur128_SOURCE_DIR}/ebur128"
                       INTERFACE_LINK_LIBRARIES ebur128
        )
    endif()
endif()
