project(srt-xtransmit)

option(ENABLE_CXX17 "Should the c++17 parts (file receive/send) be enabled" OFF)

#AUX_SOURCE_DIRECTORY(SOURCES ./) 
FILE(GLOB SOURCES *.cpp *.hpp)

add_executable(srt-xtransmit ${SOURCES})

set(QUICHE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/submodule/quiche/include")
set(QUICHE_LIB_DIR "${CMAKE_SOURCE_DIR}/submodule/quiche/lib/debug")

message(STATUS "SRT_INCLUDE_DIR: ${SRT_INCLUDE_DIR}")
message(STATUS "SRT_UTILS_INCLUDE_DIR: ${SRT_UTILS_INCLUDE_DIR}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "VIRTUAL_srtsupport: ${VIRTUAL_srtsupport}")
target_include_directories(srt-xtransmit PUBLIC
	${SRT_INCLUDE_DIR}
	${SRT_UTILS_INCLUDE_DIR}
	${QUICHE_INCLUDE_DIR}
	${PTHREAD_INCLUDE_DIR}
	)

if (ENABLE_BONDING)
	target_compile_options(srt-xtransmit
		PRIVATE "-DENABLE_BONDING=1"
	)
endif()

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
	PRIVATE ${TARGET_srt}_static ${VIRTUAL_srtsupport} ${LINKSTDCPP_FS}
	PRIVATE quiche
)

if (WIN32)
	target_link_libraries(srt-xtransmit
		PRIVATE crypt32 Userenv Bcrypt
	)
endif()


target_link_directories(srt-xtransmit
	PRIVATE ${QUICHE_LIB_DIR}
)



set_target_properties(srt-xtransmit
	PROPERTIES
	CXX_STANDARD ${REQUIRE_CXX_VER}
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

