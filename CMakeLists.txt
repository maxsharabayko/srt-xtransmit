cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(SRT-XTRANSMIT C CXX)


add_subdirectory(submodule/CLI11)
# Adds ${CLI11_SOURCE_DIR} etc.


SET(ENABLE_APPS ON)
SET(ENABLE_SHARED OFF)
add_subdirectory(submodule/srt)

#set_target_properties(srt-live-transmit
#	PROPERTIES
#	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
#	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
#	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
#)

SET(SRT_INCLUDE_DIR
	${CMAKE_SOURCE_DIR}/submodule/srt/srtcore
	${CMAKE_BINARY_DIR}/submodule/srt
	${CMAKE_SOURCE_DIR}/submodule/srt/common
	${CMAKE_SOURCE_DIR}/submodule/srt/haicrypt
)

SET(SRT_UTILS_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/submodule/srt/apps ${CMAKE_SOURCE_DIR}/submodule/srt/testing)
add_subdirectory(xtransmit)
