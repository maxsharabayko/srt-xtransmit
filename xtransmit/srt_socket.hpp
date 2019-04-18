#pragma once


namespace xtransmit {
namespace udp {

class socket
{
public:

    void listen();
    void async_connect();
    void async_accept();

public:

    void async_read();
    void async_write();

};

} // namespace srt
} // namespace xtransmit

