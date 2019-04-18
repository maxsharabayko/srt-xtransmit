cmake_minimum_required(VERSION 3.11 FATAL_ERROR)
project(SRT-XTRANSMIT C CXX)


add_subdirectory(submodule/function2)
add_subdirectory(submodule/CLI11)
# Adds ${CLI11_SOURCE_DIR} etc.
add_subdirectory(submodule/continuable)

SET(ENABLE_APPS ON)
SET(ENABLE_SHARED OFF)
add_subdirectory(submodule/srt)

SET(SRT_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/submodule/srt/srtcore ${CMAKE_BINARY_DIR}/submodule/srt ${CMAKE_SOURCE_DIR}/submodule/srt/common)
SET(SRT_UTILS_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/submodule/srt/apps)
add_subdirectory(xtransmit)
