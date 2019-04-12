project(srt-xtransmit)
set(SOURCES
		xtransmit-app.cpp)

add_executable(srt-xtransmit ${SOURCES})

include_directories(srt-xtransmit ${SRT_SRC_SRTCORE_DIR})
target_link_libraries(srt-xtransmit continuable CLI11::CLI11 ${TARGET_srt}_static)

