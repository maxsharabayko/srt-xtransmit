cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(SRT-XTRANSMIT C CXX)


add_subdirectory(submodule/CLI11)

add_subdirectory(submodule/spdlog)
# Adds ${CLI11_SOURCE_DIR} etc.

# BOOST is not used at the moment
#
#if (NOT WIN32 AND NOT MINGW)
#	find_package(Threads REQUIRED)
#	set(PTHREAD_LIBRARY ${CMAKE_THREAD_LIBS_INIT})
#	find_package(Boost 1.70 COMPONENTS date_time regex REQUIRED )
#elseif(WIN32)
#	set (Boost_THREADAPI win32)
#	set (Boost_COMPILER "-vc")
#	set (Boost_USE_STATIC_RUNTIME ON)
#	find_package(Boost 1.70)
#endif()
#
#include_directories(${Boost_INCLUDE_DIR})
#link_directories(${Boost_INCLUDE_DIR}/stage/lib)

IF (NOT DEFINED WITH_SRT_DIR)
SET(WITH_SRT_DIR submodule/srt)
ENDIF()

SET(ENABLE_APPS ON)
SET(ENABLE_SHARED OFF)
SET(ENABLE_ENCRYPTION ON)
add_subdirectory(${WITH_SRT_DIR})

set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(submodule/nlohmann_json)

#set_target_properties(srt-live-transmit
#	PROPERTIES
#	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
#	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
#	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
#)

SET(SRT_INCLUDE_DIR
	${CMAKE_SOURCE_DIR}/${WITH_SRT_DIR}/srtcore
	${CMAKE_BINARY_DIR}/${WITH_SRT_DIR}
	${CMAKE_SOURCE_DIR}/${WITH_SRT_DIR}/common
	${CMAKE_SOURCE_DIR}/${WITH_SRT_DIR}/haicrypt
)

SET(SRT_UTILS_INCLUDE_DIR
	${CMAKE_SOURCE_DIR}/${WITH_SRT_DIR}/apps
	${CMAKE_SOURCE_DIR}/${WITH_SRT_DIR}/testing
)

add_subdirectory(xtransmit)
