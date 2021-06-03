project(srt-xtransmit)

option(ENABLE_CXX17 "Should the c++17 parts (file receive/send) be enabled" OFF)

#AUX_SOURCE_DIRECTORY(SOURCES ./) 
FILE(GLOB SOURCES *.cpp *.hpp *.c)

SET(PICOTLS_OPENSSL_FILES
	${CMAKE_SOURCE_DIR}/submodule/quicly/deps/picotls/lib/openssl.c
	${CMAKE_SOURCE_DIR}/submodule/quicly/deps/picotls/lib/pembase64.c
	${CMAKE_SOURCE_DIR}/submodule/quicly/deps/picotls/lib/picotls.c)

add_executable(srt-xtransmit ${SOURCES} ${PICOTLS_OPENSSL_FILES})

message(STATUS "SRT_INCLUDE_DIR: ${SRT_INCLUDE_DIR}")
message(STATUS "SRT_UTILS_INCLUDE_DIR: ${SRT_UTILS_INCLUDE_DIR}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "VIRTUAL_srtsupport: ${VIRTUAL_srtsupport}")
message(STATUS "QUICLY_INCLUDE_DIR: ${QUICLY_INCLUDE_DIR}")
target_include_directories(srt-xtransmit PUBLIC
	${SRT_INCLUDE_DIR}
	${SRT_UTILS_INCLUDE_DIR}
	${CMAKE_SOURCE_DIR}/submodule/quicly/include
	${CMAKE_SOURCE_DIR}/submodule/quicly/deps/picotls/include
	${OPENSSL_INCLUDE_DIR} # picotls/utils.h includes openSSL header
	${PTHREAD_INCLUDE_DIR}
	)


if (ENABLE_CXX17)
	set(REQUIRE_CXX_VER 17)
	if (${CMAKE_CXX_COMPILER_ID} MATCHES "GNU|Intel")
		target_compile_options(srt-xtransmit PRIVATE "-lstdc++fs")
		set(LINKSTDCPP_FS stdc++fs)
	endif()
	target_compile_options(srt-xtransmit PRIVATE "-DENABLE_FILE_TRANSFER" "-DENABLE_CXX17")
	message(STATUS "C++17 features: ENABLED")
else()
	set(REQUIRE_CXX_VER 14)
	message(STATUS "C++17 features: DISABLED")
endif()

set (VIRTUAL_srtsupport $<TARGET_OBJECTS:srtsupport_virtual>)
#spdlog_enable_warnings(srt-xtransmit)
target_link_libraries(srt-xtransmit
	PRIVATE CLI11::CLI11
	PRIVATE spdlog::spdlog
	quicly
	PRIVATE ${TARGET_srt}_static ${VIRTUAL_srtsupport} ${LINKSTDCPP_FS})

set_target_properties(srt-xtransmit
	PROPERTIES
	CXX_STANDARD ${REQUIRE_CXX_VER}
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

