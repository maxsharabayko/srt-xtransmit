#include <thread>
#include <list>
#include <queue>
#include <atomic>
#include <mutex>
#include <future>
#include "srt.h"
#include "uriparser.hpp"
//#include "testmedia.hpp"
#include "utilities.h"

using namespace std;

class SrtNode
{

public:

    SrtNode(const UriParser &src_uri);

    ~SrtNode();


public:

    int Listen(int max_conn);

    int Connect();

    std::future<SRTSOCKET> AcceptConnection(const volatile std::atomic_bool& force_break);

    void Close() { m_stop_accept = true; }

public:

    // Receive data
    // return     -2 unexpected error
    //            -1 SRT error
    //
    int Receive(char *buffer, size_t buffer_len, int *srt_socket_id);


    int Send(const char *buffer, size_t buffer_len, int srt_socket_id);


    int Send(const char *buffer, size_t buffer_len);


    int WaitUndelivered(int wait_ms);

    SRTSOCKET GetBindSocket() { return m_bindsock; }

private:

    int EstablishConnection(bool caller, int max_conn);

    SRTSOCKET AcceptingThread(const volatile std::atomic_bool& force_break);

    SRTSOCKET AcceptNewClient();

    int ConfigurePre(SRTSOCKET sock);
    int ConfigureAcceptedSocket(SRTSOCKET sock);

private:

    std::list<SRTSOCKET>      m_read_fifo;
    std::set<SRTSOCKET>       m_accepted_sockets;

    SRTSOCKET                 m_bindsock = SRT_INVALID_SOCK;
    int                       m_epoll_accept = -1;
    int                       m_epoll_receive = -1;
    bool                      m_accept_once = false;

    std::promise<void>        m_accept_barrier;

private:

    std::atomic<bool> m_stop_accept = { false };
    std::mutex        m_recv_mutex;
    std::mutex        m_accepted_sockets_mutex;

    std::thread m_accepting_thread;

private:    // Configuration

    std::string m_host;
    int m_port;
    std::map<string, string> m_options; // All other options, as provided in the URI


};


