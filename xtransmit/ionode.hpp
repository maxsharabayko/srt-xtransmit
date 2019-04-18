#pragma once


namespace xtransmit {
namespace io {

    class node
    {

    public:

        void open();
        void close();

    public:

        void listen();
        void async_connect();
        void async_accept();

    public:

        void async_read();
        void async_write();

    };

} // namespace io
} // namespace xtransmit


