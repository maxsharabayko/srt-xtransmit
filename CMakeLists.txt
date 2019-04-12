cmake_minimum_required(VERSION 3.11 FATAL_ERROR)
project(SRT-XTRANSMIT C CXX)


add_subdirectory(submodule/function2)
add_subdirectory(submodule/CLI11)
# Adds ${CLI11_SOURCE_DIR} etc.
add_subdirectory(submodule/continuable)

SET(ENABLE_APPS OFF)
SET(ENABLE_SHARED OFF)
add_subdirectory(submodule/srt)


add_subdirectory(xtransmit)
