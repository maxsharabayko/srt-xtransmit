#pragma once
#include <memory>
#include <vector>
#include "srt_socket.hpp"



std::vector<std::future<void>> accepting_threads;


void inline async_accept(std::shared_ptr< xtransmit::srt::socket> s)
{
	// 1. Wait for acccept
	auto s_accepted = s->async_accept();
	// catch exception (error)
	
	// if ok start another async_accept
	accepting_threads.push_back(
		async(std::launch::async, &async_accept, s)
	);

	// Start some socket operation
}
