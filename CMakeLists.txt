cmake_minimum_required(VERSION 3.16)

project(runml)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(_pkg_cppunit
    REQUIRED IMPORTED_TARGET
    cppunit
)

set(runml_src_test
    test/main.cc
)

# set(runml_src_lib
# )

set(runml_src_main
    src/main.c
)

# add_library(runml_lib SHARED
#     ${runml_src_lib}
# )

add_executable(runml_main
    ${runml_src_main}
)

target_link_libraries(runml_main
    # PRIVATE runml_lib
)

add_executable(runml_test
    ${runml_src_test}
)

target_include_directories(runml_test
    PRIVATE src
)

target_link_libraries(runml_test
    # PRIVATE runml_lib
    PkgConfig::_pkg_cppunit
)

target_precompile_headers(runml_test
    PRIVATE
        <algorithm>
        <cppunit/extensions/HelperMacros.h>
)

enable_testing()
