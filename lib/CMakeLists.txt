cmake_minimum_required(VERSION 3.11)

project(hope-proto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB CL_HEADERS
    hope_proto/*.h
)

add_library(hope-proto INTERFACE ${CL_HEADERS})
target_include_directories(hope-proto INTERFACE ../lib)
