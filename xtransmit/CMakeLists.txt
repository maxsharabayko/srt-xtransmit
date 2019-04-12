project(srt-xtransmit)

#AUX_SOURCE_DIRECTORY(SOURCES ./) 
FILE(GLOB SOURCES *.cpp *.hpp)

#set(SOURCES
#		xtransmit-app.cpp)

add_executable(srt-xtransmit ${SOURCES})

message(STATUS "SRT_INCLUDE_DIR: ${SRT_INCLUDE_DIR}")
message(STATUS "SRT_UTILS_INCLUDE_DIR: ${SRT_UTILS_INCLUDE_DIR}")
include_directories(srt-xtransmit ${SRT_INCLUDE_DIR} ${SRT_UTILS_INCLUDE_DIR})
target_link_libraries(srt-xtransmit continuable CLI11::CLI11 ${TARGET_srt}_static)

