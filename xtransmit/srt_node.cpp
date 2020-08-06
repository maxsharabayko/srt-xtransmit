#include <assert.h>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <array>
#include <future>
#include "apputil.hpp"  // CreateAddrInet
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "logsupport.hpp"
#include "verbose.hpp"
#include "srt_node.hpp"


using namespace std;



inline std::string print_ts()
{
    time_t time = chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tm *tm  = localtime(&time);
    time_t usec = time % 1000000;

    char tmp_buf[512];
#ifdef _WIN32
    strftime(tmp_buf, 512, "%T.", tm);
#else
    strftime(tmp_buf, 512, "%T.", tm);
#endif
    ostringstream out;
    out << tmp_buf << setfill('0') << setw(6) << usec << " ";
    return out.str();
}



SrtNode::SrtNode(const UriParser &src_uri)
    : m_host(src_uri.host())
    , m_port(src_uri.portno())
    , m_options(src_uri.parameters())
{
    srt_startup();

    m_epoll_accept  = srt_epoll_create();
    if (m_epoll_accept == -1)
        throw std::runtime_error("Can't create epoll in nonblocking mode");
    m_epoll_receive = srt_epoll_create();
    if (m_epoll_receive == -1)
        throw std::runtime_error("Can't create epoll in nonblocking mode");
}


SrtNode::~SrtNode()
{
    m_stop_accept = true;
    if (m_accepting_thread.joinable())
        m_accepting_thread.join();

    lock_guard<mutex> lock(m_recv_mutex);
    Verb() << "[SrtNode] Desctuctor: closing socket " << m_bindsock;
    srt_close(m_bindsock);
}


std::future<SRTSOCKET> SrtNode::AcceptConnection(const volatile std::atomic_bool& force_break)
{
    return async(std::launch::async, &SrtNode::AcceptingThread, this, std::ref(force_break));
}



SRTSOCKET SrtNode::AcceptingThread(const volatile std::atomic_bool& force_break)
{
    int rnum = 2;
    SRTSOCKET read_fds[2] = {};

    while (!force_break)
    {
        const int epoll_res 
            = srt_epoll_wait(m_epoll_accept, read_fds, &rnum, 0, 0, 500,
                                                    0,     0, 0, 0);

        if (epoll_res > 0)
        {
            Verb() << "AcceptingThread: epoll res " << epoll_res << " rnum: " << rnum;
            const SRTSOCKET sock = AcceptNewClient();
            if (sock != SRT_INVALID_SOCK)
            {
                m_accepted_sockets_mutex.lock();
                if (m_accepted_sockets.insert(sock).second == false)
                    cerr << "WARN. Socket " << sock << " is already in the list. Unexpected.\n";
                m_accepted_sockets_mutex.unlock();
                
                const int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                const int res = srt_epoll_add_usock(m_epoll_receive, sock, &events);
                Verb() << print_ts() << "AcceptingThread: added socket " << sock << " tp epoll res " << res;
                Verb() << "m_accepted_sockets: " << m_accepted_sockets.size();
                return sock;
            }
        }
    }

    return SRT_ERROR;
}


SRTSOCKET SrtNode::AcceptNewClient()
{
    sockaddr_in scl;
    int sclen = sizeof scl;

    Verb() << " accept..." << VerbNoEOL;

    const SRTSOCKET socket = srt_accept(m_bindsock, (sockaddr*)&scl, &sclen);
    if (socket == SRT_INVALID_SOCK)
    {
        Verb() << "Accept failed: " << srt_getlasterror_str();
        return socket;
    }

    Verb() << "Connected socket " << socket;

    // ConfigurePre is done on bindsock, so any possible Pre flags
    // are DERIVED by sock. ConfigurePost is done exclusively on sock.
    const int stat = ConfigureAcceptedSocket(socket);
    if (stat == SRT_ERROR)
        Verb() << "ConfigureAcceptedSocket failed: " << srt_getlasterror_str();

    return socket;
}



int SrtNode::ConfigureAcceptedSocket(SRTSOCKET sock)
{
    bool no = false;
    const int yes = 1;
    int result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &no, sizeof no);
    if (result == -1)
        return result;

    result = srt_setsockopt(sock, 0, SRTO_SNDSYN, &yes, sizeof yes);
    if (result == -1)
        return result;

    //if (m_timeout)
    //    return srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &m_timeout, sizeof m_timeout);

    SrtConfigurePost(sock, m_options);

    return 0;
}


int SrtNode::ConfigurePre(SRTSOCKET sock)
{
    const int no  = 0;
    const int yes = 1;

    int result = 0;
    result = srt_setsockopt(sock, 0, SRTO_TSBPDMODE, &no, sizeof no);
    if (result == -1)
        return result;

    // Non-blocking receiving mode
    result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &no, sizeof no);
    if (result == -1)
        return result;

    // Blocking sending mode
    result = srt_setsockopt(sock, 0, SRTO_SNDSYN, &yes, sizeof yes);
    if (result == -1)
        return result;

    // host is only checked for emptiness and depending on that the connection mode is selected.
    // Here we are not exactly interested with that information.
    vector<string> failures;

    // NOTE: here host = "", so the 'connmode' will be returned as LISTENER always,
    // but it doesn't matter here. We don't use 'connmode' for anything else than
    // checking for failures.
    SocketOption::Mode conmode = SrtConfigurePre(sock, "", m_options, &failures);

    if (conmode == SocketOption::FAILURE)
    {
        if (Verbose::on)
        {
            Verb() << "WARNING: failed to set options: ";
            copy(failures.begin(), failures.end(), ostream_iterator<string>(*Verbose::cverb, ", "));
            Verb();
        }

        return SRT_ERROR;
    }

    return 0;
}


int SrtNode::EstablishConnection(bool caller, int max_conn)
{
    m_bindsock = srt_create_socket();

    if (m_bindsock == SRT_INVALID_SOCK)
        return SRT_ERROR;

    int stat = ConfigurePre(m_bindsock);
    if (stat == SRT_ERROR)
        return SRT_ERROR;

    const int modes = SRT_EPOLL_IN;
    srt_epoll_add_usock(m_epoll_accept, m_bindsock, &modes);

    sockaddr_any sa = CreateAddr(m_host, m_port);
    sockaddr* psa = (sockaddr*)&sa;

    if (caller)
    {
        const int no = 0;
        const int yes = 1;

        int result = 0;
        result = srt_setsockopt(m_bindsock, 0, SRTO_RCVSYN, &yes, sizeof yes);
        if (result == -1)
            return result;

        Verb() << "Connecting to " << m_host << ":" << m_port << " ... " << VerbNoEOL;
        int stat = srt_connect(m_bindsock, psa, sizeof sa);
        if (stat == SRT_ERROR)
        {
            srt_close(m_bindsock);
            Verb() << " failed: " << srt_getlasterror_str();
            return SRT_ERROR;
        }

        result = srt_setsockopt(m_bindsock, 0, SRTO_RCVSYN, &no, sizeof no);
        if (result == -1)
        {
            Verb() << " failed while setting socket options: " << srt_getlasterror_str();
            return result;
        }

        const int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
        srt_epoll_add_usock(m_epoll_receive, m_bindsock, &events);
        Verb() << " suceeded";
    }
    else
    {
        Verb() << "Binding a server on " << m_host << ":" << m_port << VerbNoEOL;
        stat = srt_bind(m_bindsock, psa, sizeof sa);
        if (stat == SRT_ERROR)
        {
            srt_close(m_bindsock);
            return SRT_ERROR;
        }

        stat = srt_listen(m_bindsock, max_conn);
        if (stat == SRT_ERROR)
        {
            srt_close(m_bindsock);
            Verb() << ", but listening failed with " << srt_getlasterror_str();
            return SRT_ERROR;
        }

        Verb() << " and listening";

        //m_accepting_thread = thread(&SrtNode::AcceptingThread, this);
    }

    return 0;
}


int SrtNode::Listen(int max_conn)
{
    return EstablishConnection(false, max_conn);
}


int SrtNode::Connect()
{
    return EstablishConnection(true, 1);
}



int SrtNode::Receive(char *buffer, size_t buffer_len, int *srt_socket_id)
{
    const int wait_ms = 3000;
    while (!m_stop_accept)
    {
        lock_guard<mutex> lock(m_recv_mutex);

        array< SRTSOCKET, 2 > read_fds  = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };
        array< SRTSOCKET, 2 > write_fds = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };

        int rnum = (int) read_fds .size();
        int wnum = (int) write_fds.size();

        const int epoll_res = srt_epoll_wait(m_epoll_receive,
            read_fds.data(),  &rnum,
            write_fds.data(), &wnum,
            wait_ms, 0, 0, 0, 0);

        if (epoll_res <= 0)    // Wait timeout
            continue;

        assert(rnum > 0);
        assert(wnum <= rnum);

        if (false/*Verbose::on*/)
        {
            // Verbose info:
            Verb() << "[SrNode] Received epoll_res " << epoll_res;
            Verb() << "   to read  " << rnum << ": " << VerbNoEOL;
            copy(read_fds.begin(), next(read_fds.begin(), rnum),
                ostream_iterator<int>(*Verbose::cverb, ", "));
            Verb();
            Verb() << "   to write " << wnum << ": " << VerbNoEOL;
            copy(write_fds.begin(), next(write_fds.begin(), wnum),
                ostream_iterator<int>(*Verbose::cverb, ", "));
            Verb();
        }

        // If this is true, it is really unexpected.
        if (rnum <= 0 && wnum <= 0)
            return -2;

        const SRTSOCKET sock = read_fds[0];

        const int recv_res = srt_recvmsg2(sock, buffer, (int)buffer_len, nullptr);
        Verb() << print_ts() << "[SrNode] Read from socket " << sock << " resulted with " << recv_res;

        if (recv_res > 0)
        {
            if (srt_socket_id != nullptr)
                *srt_socket_id = sock;
            return recv_res;
        }

        const int srt_err = srt_getlasterror(nullptr);
        if (srt_err == SRT_ECONNLOST)   // Broken || Closing
        {
            Verb() << print_ts() << "[SrNode] Socket " << sock << " lost connection. Remove from epoll.";
            srt_close(sock);
            m_accepted_sockets_mutex.lock();
            if (1 != m_accepted_sockets.erase(sock))
                cerr << "WARN. During erasure of sock " << sock << endl;
            m_accepted_sockets_mutex.unlock();
            return 0;
        }
        else if (srt_err == SRT_EINVSOCK)
        {
            Verb() << print_ts() << "[SrNode] Socket " << sock << " is no longer valid (state "
                    << srt_getsockstate(sock) << "). Remove from epoll.";
            srt_epoll_remove_usock(m_epoll_receive, sock);
            m_accepted_sockets_mutex.lock();
            if (1 != m_accepted_sockets.erase(sock))
                cerr << "[SrNode] WARN. During erasure of sock " << sock << endl;
            m_accepted_sockets_mutex.unlock();
            return 0;
        }

        // An error happened. Notify the caller of the function.
        if (srt_socket_id != nullptr)
            *srt_socket_id = sock;
        return recv_res;
    }

    return 0;
}



int SrtNode::WaitUndelivered(int wait_ms)
{
    const SRTSOCKET sock = GetBindSocket();

    const SRT_SOCKSTATUS status = srt_getsockstate(sock);
    if (status != SRTS_CONNECTED && status != SRTS_CLOSING)
        return 0;

    size_t blocks = 0;
    size_t bytes = 0;
    int ms_passed = 0;
    do
    {
        if (SRT_ERROR == srt_getsndbuffer(sock, &blocks, &bytes))
            return SRT_ERROR;

        if (wait_ms == 0)
            break;

        if (wait_ms != -1 && ms_passed >= wait_ms)
            break;

        if (blocks)
            this_thread::sleep_for(chrono::milliseconds(1));
        ++ms_passed;
    } while (blocks != 0);

    return bytes;
};



int SrtNode::Send(const char *buffer, size_t buffer_len, int srt_socket_id)
{
    return srt_sendmsg(srt_socket_id, buffer, (int)buffer_len, -1, true);
}


int SrtNode::Send(const char *buffer, size_t buffer_len)
{
    return srt_sendmsg(m_bindsock, buffer, (int)buffer_len, -1, true);
}



