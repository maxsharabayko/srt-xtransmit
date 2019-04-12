project(srt-xtransmit)
set(SOURCES
		xtransmit-app.cpp)

add_executable(srt-xtransmit ${SOURCES})

target_link_libraries(srt-xtransmit continuable CLI11::CLI11)

