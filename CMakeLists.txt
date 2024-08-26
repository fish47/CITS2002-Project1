cmake_minimum_required(VERSION 3.2)

project(runml)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(runml_src
    src/main.c
)

add_executable(main ${runml_src})
