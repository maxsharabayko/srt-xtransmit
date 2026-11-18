project(srt-xtransmit)

add_executable(srt-xtransmit)

target_sources(srt-xtransmit PRIVATE
	xtransmit-app.cpp
	acceptor.hpp
	buffer.hpp
	file-forward.cpp
	file-receive.cpp
	file-receive.hpp
	file-send.cpp
	file-send.hpp
	forward.h
	generate.cpp
	generate.hpp
	io-generate.hpp
	ionode.hpp
	metrics.cpp
	metrics.hpp
	metrics_delay_factor.cpp
	metrics_delay_factor.hpp
	metrics_integrity.hpp
	metrics_jitter.cpp
	metrics_jitter.hpp
	metrics_latency.cpp
	metrics_latency.hpp
	metrics_reorder.hpp
	metrics_writer.cpp
	metrics_writer.hpp
	misc.cpp
	misc.hpp
	netaddr_any.hpp
	pacer.hpp
	receive.cpp
	receive.hpp
	route.cpp
	route.hpp
	scheduler.hpp
	socket.hpp
	socket_stats.cpp
	socket_stats.hpp
	srt_node.cpp
	srt_node.hpp
	srt_socket.cpp
	srt_socket.hpp
	srt_socket_group.cpp
	srt_socket_group.hpp
	tcp_socket.cpp
	tcp_socket.hpp
	udp_socket.cpp
	udp_socket.hpp
	xtr_defs.hpp
)

message(STATUS "SRT_INCLUDE_DIR: ${SRT_INCLUDE_DIR}")
message(STATUS "SRT_UTILS_INCLUDE_DIR: ${SRT_UTILS_INCLUDE_DIR}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "VIRTUAL_srtsupport: ${VIRTUAL_srtsupport}")
message(STATUS "SSL_LIBRARY_DIRS: ${SSL_LIBRARY_DIRS}")
target_include_directories(srt-xtransmit PUBLIC
	${SRT_INCLUDE_DIR}
	${SRT_UTILS_INCLUDE_DIR}
	${PTHREAD_INCLUDE_DIR}
	${SSL_INCLUDE_DIRS}
	)

target_link_directories(srt-xtransmit PUBLIC ${SSL_LIBRARY_DIRS})

if (ENABLE_BONDING OR ENABLE_EXPERIMENTAL_BONDING)
	target_compile_definitions(srt-xtransmit PRIVATE ENABLE_BONDING=1 ENABLE_EXPERIMENTAL_BONDING=1)
endif()

if (ENABLE_AEAD_API_PREVIEW)
	target_compile_definitions(srt-xtransmit PRIVATE ENABLE_AEAD_API_PREVIEW=1)
endif()

if (ENABLE_MAXREXMITBW)
	target_compile_definitions(srt-xtransmit PRIVATE ENABLE_MAXREXMITBW=1)
endif()

target_compile_definitions(srt-xtransmit PRIVATE ENABLE_FILE_TRANSFER=1)

set (VIRTUAL_srtsupport $<TARGET_OBJECTS:srtsupport_virtual>)
#spdlog_enable_warnings(srt-xtransmit)
target_link_libraries(srt-xtransmit
	PRIVATE CLI11::CLI11
	PRIVATE spdlog::spdlog
	PRIVATE ${TARGET_srt}_static ${VIRTUAL_srtsupport}
	PRIVATE nlohmann_json::nlohmann_json)



set_target_properties(srt-xtransmit
	PROPERTIES
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_compile_features(srt-xtransmit PRIVATE cxx_std_20)
