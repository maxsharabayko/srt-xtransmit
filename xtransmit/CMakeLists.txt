project(srt-xtransmit)

#AUX_SOURCE_DIRECTORY(SOURCES ./) 
FILE(GLOB SOURCES *.cpp *.hpp)


add_executable(srt-xtransmit ${SOURCES})

message(STATUS "SRT_INCLUDE_DIR: ${SRT_INCLUDE_DIR}")
message(STATUS "SRT_UTILS_INCLUDE_DIR: ${SRT_UTILS_INCLUDE_DIR}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "VIRTUAL_srtsupport: ${VIRTUAL_srtsupport}")
include_directories(srt-xtransmit ${SRT_INCLUDE_DIR} ${SRT_UTILS_INCLUDE_DIR} ${PTHREAD_INCLUDE_DIR})

set (VIRTUAL_srtsupport $<TARGET_OBJECTS:srtsupport_virtual>)
target_link_libraries(srt-xtransmit CLI11::CLI11 ${TARGET_srt}_static ${VIRTUAL_srtsupport})

