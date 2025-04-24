cmake_minimum_required(VERSION 3.18)
project(fastmemcpy)

add_library(
    fastmemcpy
    STATIC
        fastmemcpy.c
        fastmemcpy.h
)

target_include_directories(
        fastmemcpy
        PUBLIC
        .
)

target_link_options(
        fastmemcpy
        PUBLIC
        /machine:i386
)
