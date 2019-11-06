project(srt-xtransmit)

#AUX_SOURCE_DIRECTORY(SOURCES ./) 
FILE(GLOB SOURCES *.cpp *.hpp)


add_executable(srt-xtransmit ${SOURCES})

message(STATUS "SRT_INCLUDE_DIR: ${SRT_INCLUDE_DIR}")
message(STATUS "SRT_UTILS_INCLUDE_DIR: ${SRT_UTILS_INCLUDE_DIR}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "VIRTUAL_srtsupport: ${VIRTUAL_srtsupport}")
target_include_directories(srt-xtransmit PUBLIC
	${SRT_INCLUDE_DIR}
	${SRT_UTILS_INCLUDE_DIR}
	${PTHREAD_INCLUDE_DIR}
	)

if (${CMAKE_CXX_COMPILER_ID} MATCHES "GNU|Intel|Clang|AppleClang")
	set(LINKSTDCPP_FS stdc++fs)
endif()

set (VIRTUAL_srtsupport $<TARGET_OBJECTS:srtsupport_virtual>)
target_compile_options(srt-xtransmit PRIVATE "-lstdc++fs")
target_link_libraries(srt-xtransmit CLI11::CLI11 ${TARGET_srt}_static ${VIRTUAL_srtsupport} ${LINKSTDCPP_FS})

set_target_properties(srt-xtransmit
	PROPERTIES
	CXX_STANDARD 17
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

