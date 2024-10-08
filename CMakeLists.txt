cmake_minimum_required(VERSION 3.16)

project(runml)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RUNML_DIST_LABEL_ID1 "" CACHE STRING "the #1 stduent ID in the combined source code")
set(RUNML_DIST_LABEL_ID2 "" CACHE STRING "the #2 stduent ID in the combined source code")
set(RUNML_DIST_LABEL_NAME1 "" CACHE STRING "the #1 stduent name in the combined source code")
set(RUNML_DIST_LABEL_NAME2 "" CACHE STRING "the #2 stduent name in the combined source code")

find_package(PkgConfig REQUIRED)

pkg_check_modules(_pkg_cppunit
    REQUIRED IMPORTED_TARGET
    cppunit
)

set(runml_src_test
    test/base.h
    test/base.cc
    test/main.cc
    test/test_token.cc
    test/test_compile.cc
    test/test_exec.cc
)

set(runml_src_lib
    src/ml_memory.h
    src/ml_token.h
    src/ml_token.c
    src/ml_compile.h
    src/ml_compile.c
    src/ml_codegen.h
    src/ml_codegen.c
    src/ml_exec.h
    src/ml_exec.c
)

set(runml_src_main
    src/ml_memory.c
    src/main.c
)

add_library(runml_lib SHARED
    ${runml_src_lib}
)

target_compile_options(runml_lib
    PUBLIC -Wall -Werror
)

add_executable(runml_main
    ${runml_src_main}
)

target_link_libraries(runml_main
    PRIVATE runml_lib
)

add_executable(runml_test
    ${runml_src_test}
)

target_include_directories(runml_test
    PRIVATE src
)

target_link_libraries(runml_test
    PRIVATE runml_lib
    PkgConfig::_pkg_cppunit
)

target_precompile_headers(runml_test
    PRIVATE
        <algorithm>
        <cppunit/extensions/HelperMacros.h>
)

enable_testing()

add_custom_target(runml_dist
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND python build.py
            -o ${CMAKE_CURRENT_BINARY_DIR}/runml.c
            -i ${runml_src_lib} ${runml_src_main}
            -l ${RUNML_DIST_LABEL_ID1} ${RUNML_DIST_LABEL_NAME1}
               ${RUNML_DIST_LABEL_ID2} ${RUNML_DIST_LABEL_NAME2}
)
