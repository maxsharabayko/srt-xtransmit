#include "prometheus_exporter.hpp"
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "httplib.h"
#include "spdlog/spdlog.h"
#include "srt_socket.hpp"

namespace xtransmit
{
namespace prometheus
{

exporter::exporter(
    int port,
    const std::string& direction)
    : m_port(port)
    , m_direction(direction)
{
    if (m_port < 1 || m_port > 65535)
    {
        throw std::invalid_argument(
            "Invalid Prometheus exporter TCP port");
    }

    if (m_direction != "input" &&
        m_direction != "output")
    {
        throw std::invalid_argument(
            "Invalid Prometheus direction");
    }
}

exporter::~exporter()
{
    stop();
}



void exporter::add_socket(
    const std::shared_ptr<socket::isocket>& sock)
{
    if (!sock)
        return;

    auto srt_sock =
        std::dynamic_pointer_cast<socket::srt>(sock);

    if (!srt_sock)
        return;

    const int socket_id = srt_sock->id();

    {
        std::lock_guard<std::mutex> lock(
            m_socket_mutex);

        m_sockets[socket_id] = srt_sock;
    }

    spdlog::info(
        "PROMETHEUS Added SRT socket @{} ({})",
        socket_id,
        m_direction);
}


void exporter::remove_socket(int socket_id)
{
    size_t removed = 0;

    {
        std::lock_guard<std::mutex> lock(
            m_socket_mutex);

        removed = m_sockets.erase(socket_id);
    }

    if (removed > 0)
    {
        spdlog::info(
            "PROMETHEUS Removed SRT socket @{} ({})",
            socket_id,
            m_direction);
    }
}

std::string exporter::render_metrics()
{
    std::vector<std::shared_ptr<socket::srt>> sockets;

    /*
     * Copy the shared pointers while holding the mutex.
     * The actual SRT statistics calls are done afterwards
     * without holding the mutex.
     */
    {
        std::lock_guard<std::mutex> lock(m_socket_mutex);

        for (const auto& item : m_sockets)
        {
            if (item.second)
                sockets.push_back(item.second);
        }
    }

    std::ostringstream out;
    out << std::setprecision(15);

    /*
     * Exporter status
     */
    out <<
        "# HELP srt_xtransmit_prometheus_up "
        "Whether the Prometheus HTTP exporter is running\n"
        "# TYPE srt_xtransmit_prometheus_up gauge\n"
        "srt_xtransmit_prometheus_up 1\n";

    /*
     * Number of currently registered SRT connections
     */
    out <<
        "# HELP srt_active_connections "
        "Number of active SRT connections\n"
        "# TYPE srt_active_connections gauge\n"
        "srt_active_connections{direction=\""
        << m_direction
        << "\"} "
        << sockets.size()
        << "\n";

    /*
     * Metric descriptions
     */
    out <<
        "# HELP srt_socket_up "
        "Whether statistics for the SRT socket can be read\n"
        "# TYPE srt_socket_up gauge\n";

    out <<
        "# HELP srt_ms_rtt "
        "SRT round trip time in milliseconds\n"
        "# TYPE srt_ms_rtt gauge\n";

    out <<
        "# HELP srt_mbps_bandwidth "
        "Estimated SRT link bandwidth in megabits per second\n"
        "# TYPE srt_mbps_bandwidth gauge\n";

    out <<
        "# HELP srt_pkt_recv_total "
        "Total number of received SRT packets including retransmissions\n"
        "# TYPE srt_pkt_recv_total counter\n";

    out <<
        "# HELP srt_pkt_recv_unique_total "
        "Total number of unique received SRT packets\n"
        "# TYPE srt_pkt_recv_unique_total counter\n";

    out <<
        "# HELP srt_pkt_rcv_loss_total "
        "Total number of SRT packets detected as lost by the receiver\n"
        "# TYPE srt_pkt_rcv_loss_total counter\n";

    out <<
        "# HELP srt_pkt_rcv_drop_total "
        "Total number of SRT packets dropped by the receiver\n"
        "# TYPE srt_pkt_rcv_drop_total counter\n";

    out <<
        "# HELP srt_byte_recv_total "
        "Total number of received SRT bytes\n"
        "# TYPE srt_byte_recv_total counter\n";

    out <<
        "# HELP srt_byte_recv_unique_total "
        "Total number of unique received SRT bytes\n"
        "# TYPE srt_byte_recv_unique_total counter\n";

    out <<
        "# HELP srt_byte_rcv_loss_total "
        "Total number of SRT bytes estimated as lost\n"
        "# TYPE srt_byte_rcv_loss_total counter\n";

    out <<
        "# HELP srt_ms_rcv_buf "
        "Current SRT receiver buffer timespan in milliseconds\n"
        "# TYPE srt_ms_rcv_buf gauge\n";

    out <<
        "# HELP srt_ms_rcv_tsbpd_delay "
        "SRT receiver TSBPD delay in milliseconds\n"
        "# TYPE srt_ms_rcv_tsbpd_delay gauge\n";

    out <<
        "# HELP srt_pkt_reorder_tolerance "
        "Current SRT packet reorder tolerance\n"
        "# TYPE srt_pkt_reorder_tolerance gauge\n";

    out <<
        "# HELP srt_ms_timestamp "
        "Milliseconds since the SRT socket was started\n"
        "# TYPE srt_ms_timestamp gauge\n";

    out <<
        "# HELP srt_pkt_sent_total "
        "Total number of sent SRT data packets including retransmissions\n"
        "# TYPE srt_pkt_sent_total counter\n";

    out <<
        "# HELP srt_pkt_snd_loss_total "
        "Total number of packets considered lost by the sender\n"
        "# TYPE srt_pkt_snd_loss_total counter\n";

    out <<
        "# HELP srt_pkt_retrans_total "
        "Total number of retransmitted SRT packets\n"
        "# TYPE srt_pkt_retrans_total counter\n";

    out <<
        "# HELP srt_pkt_sent_ack_total "
        "Total number of sent SRT ACK control packets\n"
        "# TYPE srt_pkt_sent_ack_total counter\n";

    out <<
        "# HELP srt_pkt_recv_ack_total "
        "Total number of received SRT ACK control packets\n"
        "# TYPE srt_pkt_recv_ack_total counter\n";

    out <<
        "# HELP srt_pkt_sent_nak_total "
        "Total number of sent SRT NAK control packets\n"
        "# TYPE srt_pkt_sent_nak_total counter\n";

    out <<
        "# HELP srt_pkt_recv_nak_total "
        "Total number of received SRT NAK control packets\n"
        "# TYPE srt_pkt_recv_nak_total counter\n";

    out <<
        "# HELP srt_us_snd_duration_total "
        "Total sender busy time in microseconds\n"
        "# TYPE srt_us_snd_duration_total counter\n";

    out <<
        "# HELP srt_pkt_snd_drop_total "
        "Total number of too-late-to-send dropped packets\n"
        "# TYPE srt_pkt_snd_drop_total counter\n";

    out <<
        "# HELP srt_pkt_rcv_undecrypt_total "
        "Total number of received packets that could not be decrypted\n"
        "# TYPE srt_pkt_rcv_undecrypt_total counter\n";

    out <<
        "# HELP srt_byte_sent_total "
        "Total number of sent SRT bytes including retransmissions\n"
        "# TYPE srt_byte_sent_total counter\n";

    out <<
        "# HELP srt_byte_retrans_total "
        "Total number of retransmitted SRT bytes\n"
        "# TYPE srt_byte_retrans_total counter\n";

    out <<
        "# HELP srt_byte_snd_drop_total "
        "Total number of too-late-to-send dropped bytes\n"
        "# TYPE srt_byte_snd_drop_total counter\n";

    out <<
        "# HELP srt_byte_rcv_drop_total "
        "Total number of receiver dropped bytes\n"
        "# TYPE srt_byte_rcv_drop_total counter\n";

    out <<
        "# HELP srt_byte_rcv_undecrypt_total "
        "Total number of received bytes that could not be decrypted\n"
        "# TYPE srt_byte_rcv_undecrypt_total counter\n";

    out <<
        "# HELP srt_pkt_sent_unique_total "
        "Total number of unique data packets sent by the application\n"
        "# TYPE srt_pkt_sent_unique_total counter\n";

    out <<
        "# HELP srt_byte_sent_unique_total "
        "Total number of unique data bytes sent by the application\n"
        "# TYPE srt_byte_sent_unique_total counter\n";

    out <<
        "# HELP srt_pkt_snd_filter_extra_total "
        "Total number of extra control packets generated by the packet filter\n"
        "# TYPE srt_pkt_snd_filter_extra_total counter\n";

    out <<
        "# HELP srt_pkt_rcv_filter_extra_total "
        "Total number of packet-filter control packets received\n"
        "# TYPE srt_pkt_rcv_filter_extra_total counter\n";

    out <<
        "# HELP srt_pkt_rcv_filter_supply_total "
        "Total number of packets supplied by the packet filter such as FEC recovery\n"
        "# TYPE srt_pkt_rcv_filter_supply_total counter\n";

    out <<
        "# HELP srt_pkt_rcv_filter_loss_total "
        "Total number of packet losses not recoverable by the packet filter\n"
        "# TYPE srt_pkt_rcv_filter_loss_total counter\n";

    out <<
        "# HELP srt_us_pkt_snd_period "
        "Current SRT packet sending period in microseconds\n"
        "# TYPE srt_us_pkt_snd_period gauge\n";

    out <<
        "# HELP srt_pkt_flow_window "
        "Current SRT flow window size in packets\n"
        "# TYPE srt_pkt_flow_window gauge\n";

    out <<
        "# HELP srt_pkt_congestion_window "
        "Current SRT congestion window size in packets\n"
        "# TYPE srt_pkt_congestion_window gauge\n";

    out <<
        "# HELP srt_pkt_flight_size "
        "Current number of unacknowledged packets in flight\n"
        "# TYPE srt_pkt_flight_size gauge\n";

    out <<
        "# HELP srt_byte_avail_snd_buf "
        "Available sender buffer size in bytes\n"
        "# TYPE srt_byte_avail_snd_buf gauge\n";

    out <<
        "# HELP srt_byte_avail_rcv_buf "
        "Available receiver buffer size in bytes\n"
        "# TYPE srt_byte_avail_rcv_buf gauge\n";

    out <<
        "# HELP srt_mbps_max_bw "
        "Current SRT transmission bandwidth ceiling in megabits per second\n"
        "# TYPE srt_mbps_max_bw gauge\n";

    out <<
        "# HELP srt_byte_mss "
        "Current SRT maximum segment size in bytes\n"
        "# TYPE srt_byte_mss gauge\n";

    out <<
        "# HELP srt_pkt_snd_buf "
        "Current number of packets in the sender buffer\n"
        "# TYPE srt_pkt_snd_buf gauge\n";

    out <<
        "# HELP srt_byte_snd_buf "
        "Current number of bytes in the sender buffer\n"
        "# TYPE srt_byte_snd_buf gauge\n";

    out <<
        "# HELP srt_ms_snd_buf "
        "Current sender buffer timespan in milliseconds\n"
        "# TYPE srt_ms_snd_buf gauge\n";

    out <<
        "# HELP srt_ms_snd_tsbpd_delay "
        "Current sender-side TSBPD delay in milliseconds\n"
        "# TYPE srt_ms_snd_tsbpd_delay gauge\n";

    out <<
        "# HELP srt_pkt_rcv_buf "
        "Current number of packets in the receiver buffer\n"
        "# TYPE srt_pkt_rcv_buf gauge\n";

    out <<
        "# HELP srt_byte_rcv_buf "
        "Current number of bytes in the receiver buffer\n"
        "# TYPE srt_byte_rcv_buf gauge\n";

    /*
     * SRT interval-based statistics.
     *
     * These values can be reset by another call to
     * srt_bstats/srt_bistats with clear = 1.
     * Therefore they are exported as gauges, not counters.
     */

    out <<
        "# HELP srt_pkt_sent "
        "SRT packets sent since the last statistics reset\n"
        "# TYPE srt_pkt_sent gauge\n";

    out <<
        "# HELP srt_pkt_recv "
        "SRT packets received since the last statistics reset\n"
        "# TYPE srt_pkt_recv gauge\n";

    out <<
        "# HELP srt_pkt_sent_unique "
        "Unique SRT packets sent since the last statistics reset\n"
        "# TYPE srt_pkt_sent_unique gauge\n";

    out <<
        "# HELP srt_pkt_recv_unique "
        "Unique SRT packets received since the last statistics reset\n"
        "# TYPE srt_pkt_recv_unique gauge\n";

    out <<
        "# HELP srt_pkt_snd_loss "
        "Sender packet losses since the last statistics reset\n"
        "# TYPE srt_pkt_snd_loss gauge\n";

    out <<
        "# HELP srt_pkt_rcv_loss "
        "Receiver packet losses since the last statistics reset\n"
        "# TYPE srt_pkt_rcv_loss gauge\n";

    out <<
        "# HELP srt_pkt_retrans "
        "Packets retransmitted since the last statistics reset\n"
        "# TYPE srt_pkt_retrans gauge\n";

    out <<
        "# HELP srt_pkt_rcv_retrans "
        "Retransmitted packets received since the last statistics reset\n"
        "# TYPE srt_pkt_rcv_retrans gauge\n";

    out <<
        "# HELP srt_pkt_sent_ack "
        "ACK packets sent since the last statistics reset\n"
        "# TYPE srt_pkt_sent_ack gauge\n";

    out <<
        "# HELP srt_pkt_recv_ack "
        "ACK packets received since the last statistics reset\n"
        "# TYPE srt_pkt_recv_ack gauge\n";

    out <<
        "# HELP srt_pkt_sent_nak "
        "NAK packets sent since the last statistics reset\n"
        "# TYPE srt_pkt_sent_nak gauge\n";

    out <<
        "# HELP srt_pkt_recv_nak "
        "NAK packets received since the last statistics reset\n"
        "# TYPE srt_pkt_recv_nak gauge\n";

    out <<
        "# HELP srt_mbps_send_rate "
        "SRT sending rate in megabits per second\n"
        "# TYPE srt_mbps_send_rate gauge\n";

    out <<
        "# HELP srt_mbps_recv_rate "
        "SRT receiving rate in megabits per second\n"
        "# TYPE srt_mbps_recv_rate gauge\n";

    out <<
        "# HELP srt_us_snd_duration "
        "Sender busy time since the last statistics reset in microseconds\n"
        "# TYPE srt_us_snd_duration gauge\n";

    out <<
        "# HELP srt_pkt_reorder_distance "
        "SRT receiver packet reorder distance\n"
        "# TYPE srt_pkt_reorder_distance gauge\n";

    out <<
        "# HELP srt_ms_rcv_avg_belated_time "
        "Average delay of belated received packets in milliseconds\n"
        "# TYPE srt_ms_rcv_avg_belated_time gauge\n";

    out <<
        "# HELP srt_pkt_rcv_belated "
        "Packets received too late since the last statistics reset\n"
        "# TYPE srt_pkt_rcv_belated gauge\n";

    out <<
        "# HELP srt_pkt_snd_drop "
        "Sender dropped packets since the last statistics reset\n"
        "# TYPE srt_pkt_snd_drop gauge\n";

    out <<
        "# HELP srt_pkt_rcv_drop "
        "Receiver dropped packets since the last statistics reset\n"
        "# TYPE srt_pkt_rcv_drop gauge\n";

    out <<
        "# HELP srt_pkt_rcv_undecrypt "
        "Packets that could not be decrypted since the last statistics reset\n"
        "# TYPE srt_pkt_rcv_undecrypt gauge\n";

    out <<
        "# HELP srt_byte_sent "
        "Bytes sent since the last statistics reset\n"
        "# TYPE srt_byte_sent gauge\n";

    out <<
        "# HELP srt_byte_recv "
        "Bytes received since the last statistics reset\n"
        "# TYPE srt_byte_recv gauge\n";

    out <<
        "# HELP srt_byte_sent_unique "
        "Unique bytes sent since the last statistics reset\n"
        "# TYPE srt_byte_sent_unique gauge\n";

    out <<
        "# HELP srt_byte_recv_unique "
        "Unique bytes received since the last statistics reset\n"
        "# TYPE srt_byte_recv_unique gauge\n";

    out <<
        "# HELP srt_byte_rcv_loss "
        "Receiver lost bytes since the last statistics reset\n"
        "# TYPE srt_byte_rcv_loss gauge\n";

    out <<
        "# HELP srt_byte_retrans "
        "Retransmitted bytes since the last statistics reset\n"
        "# TYPE srt_byte_retrans gauge\n";

    out <<
        "# HELP srt_byte_snd_drop "
        "Sender dropped bytes since the last statistics reset\n"
        "# TYPE srt_byte_snd_drop gauge\n";

    out <<
        "# HELP srt_byte_rcv_drop "
        "Receiver dropped bytes since the last statistics reset\n"
        "# TYPE srt_byte_rcv_drop gauge\n";

    out <<
        "# HELP srt_byte_rcv_undecrypt "
        "Undecryptable received bytes since the last statistics reset\n"
        "# TYPE srt_byte_rcv_undecrypt gauge\n";

    out <<
        "# HELP srt_pkt_snd_filter_extra "
        "Packet-filter control packets sent since the last statistics reset\n"
        "# TYPE srt_pkt_snd_filter_extra gauge\n";

    out <<
        "# HELP srt_pkt_rcv_filter_extra "
        "Packet-filter control packets received since the last statistics reset\n"
        "# TYPE srt_pkt_rcv_filter_extra gauge\n";

    out <<
        "# HELP srt_pkt_rcv_filter_supply "
        "Packets supplied by the packet filter since the last statistics reset\n"
        "# TYPE srt_pkt_rcv_filter_supply gauge\n";

    out <<
        "# HELP srt_pkt_rcv_filter_loss "
        "Packet losses not recoverable by the packet filter since the last statistics reset\n"
        "# TYPE srt_pkt_rcv_filter_loss gauge\n";

    /*
     * Read statistics from every currently registered SRT socket.
     */
    for (const auto& sock : sockets)
    {
        if (!sock)
            continue;

        const int socket_id = sock->id();

        const std::string labels =
            "{direction=\"" +
            m_direction +
            "\",socket_id=\"" +
            std::to_string(socket_id) +
            "\"}";

        SRT_TRACEBSTATS stats {};

        /*
         * statistics_snapshot() uses:
         *
         * clear = 0
         * instantaneous = 1
         *
         * Therefore a Prometheus scrape does not reset SRT
         * interval statistics.
         */
        if (sock->statistics_snapshot(stats, true) == SRT_ERROR)
        {
            out <<
                "srt_socket_up"
                << labels
                << " 0\n";

            continue;
        }

        out <<
            "srt_socket_up"
            << labels
            << " 1\n";

        out <<
            "srt_ms_rtt"
            << labels
            << " "
            << stats.msRTT
            << "\n";

        out <<
            "srt_mbps_bandwidth"
            << labels
            << " "
            << stats.mbpsBandwidth
            << "\n";

        out <<
            "srt_pkt_recv_total"
            << labels
            << " "
            << stats.pktRecvTotal
            << "\n";

        out <<
            "srt_pkt_recv_unique_total"
            << labels
            << " "
            << stats.pktRecvUniqueTotal
            << "\n";

        out <<
            "srt_pkt_rcv_loss_total"
            << labels
            << " "
            << stats.pktRcvLossTotal
            << "\n";

        out <<
            "srt_pkt_rcv_drop_total"
            << labels
            << " "
            << stats.pktRcvDropTotal
            << "\n";

        out <<
            "srt_byte_recv_total"
            << labels
            << " "
            << stats.byteRecvTotal
            << "\n";

        out <<
            "srt_byte_recv_unique_total"
            << labels
            << " "
            << stats.byteRecvUniqueTotal
            << "\n";

        out <<
            "srt_byte_rcv_loss_total"
            << labels
            << " "
            << stats.byteRcvLossTotal
            << "\n";

        out <<
            "srt_ms_rcv_buf"
            << labels
            << " "
            << stats.msRcvBuf
            << "\n";

        out <<
            "srt_ms_rcv_tsbpd_delay"
            << labels
            << " "
            << stats.msRcvTsbPdDelay
            << "\n";

        out <<
            "srt_pkt_reorder_tolerance"
            << labels
            << " "
            << stats.pktReorderTolerance
            << "\n";

	        out << "srt_ms_timestamp"
            << labels << " "
            << stats.msTimeStamp << "\n";

        out << "srt_pkt_sent_total"
            << labels << " "
            << stats.pktSentTotal << "\n";

        out << "srt_pkt_snd_loss_total"
            << labels << " "
            << stats.pktSndLossTotal << "\n";

        out << "srt_pkt_retrans_total"
            << labels << " "
            << stats.pktRetransTotal << "\n";

        out << "srt_pkt_sent_ack_total"
            << labels << " "
            << stats.pktSentACKTotal << "\n";

        out << "srt_pkt_recv_ack_total"
            << labels << " "
            << stats.pktRecvACKTotal << "\n";

        out << "srt_pkt_sent_nak_total"
            << labels << " "
            << stats.pktSentNAKTotal << "\n";

        out << "srt_pkt_recv_nak_total"
            << labels << " "
            << stats.pktRecvNAKTotal << "\n";

        out << "srt_us_snd_duration_total"
            << labels << " "
            << stats.usSndDurationTotal << "\n";

        out << "srt_pkt_snd_drop_total"
            << labels << " "
            << stats.pktSndDropTotal << "\n";

        out << "srt_pkt_rcv_undecrypt_total"
            << labels << " "
            << stats.pktRcvUndecryptTotal << "\n";

        out << "srt_byte_sent_total"
            << labels << " "
            << stats.byteSentTotal << "\n";

        out << "srt_byte_retrans_total"
            << labels << " "
            << stats.byteRetransTotal << "\n";

        out << "srt_byte_snd_drop_total"
            << labels << " "
            << stats.byteSndDropTotal << "\n";

        out << "srt_byte_rcv_drop_total"
            << labels << " "
            << stats.byteRcvDropTotal << "\n";

        out << "srt_byte_rcv_undecrypt_total"
            << labels << " "
            << stats.byteRcvUndecryptTotal << "\n";

        out << "srt_pkt_sent_unique_total"
            << labels << " "
            << stats.pktSentUniqueTotal << "\n";

        out << "srt_byte_sent_unique_total"
            << labels << " "
            << stats.byteSentUniqueTotal << "\n";

        out << "srt_pkt_snd_filter_extra_total"
            << labels << " "
            << stats.pktSndFilterExtraTotal << "\n";

        out << "srt_pkt_rcv_filter_extra_total"
            << labels << " "
            << stats.pktRcvFilterExtraTotal << "\n";

        out << "srt_pkt_rcv_filter_supply_total"
            << labels << " "
            << stats.pktRcvFilterSupplyTotal << "\n";

        out << "srt_pkt_rcv_filter_loss_total"
            << labels << " "
            << stats.pktRcvFilterLossTotal << "\n";

        out << "srt_us_pkt_snd_period"
            << labels << " "
            << stats.usPktSndPeriod << "\n";

        out << "srt_pkt_flow_window"
            << labels << " "
            << stats.pktFlowWindow << "\n";

        out << "srt_pkt_congestion_window"
            << labels << " "
            << stats.pktCongestionWindow << "\n";

        out << "srt_pkt_flight_size"
            << labels << " "
            << stats.pktFlightSize << "\n";

        out << "srt_byte_avail_snd_buf"
            << labels << " "
            << stats.byteAvailSndBuf << "\n";

        out << "srt_byte_avail_rcv_buf"
            << labels << " "
            << stats.byteAvailRcvBuf << "\n";

        out << "srt_mbps_max_bw"
            << labels << " "
            << stats.mbpsMaxBW << "\n";

        out << "srt_byte_mss"
            << labels << " "
            << stats.byteMSS << "\n";

        out << "srt_pkt_snd_buf"
            << labels << " "
            << stats.pktSndBuf << "\n";

        out << "srt_byte_snd_buf"
            << labels << " "
            << stats.byteSndBuf << "\n";

        out << "srt_ms_snd_buf"
            << labels << " "
            << stats.msSndBuf << "\n";

        out << "srt_ms_snd_tsbpd_delay"
            << labels << " "
            << stats.msSndTsbPdDelay << "\n";

        out << "srt_pkt_rcv_buf"
            << labels << " "
            << stats.pktRcvBuf << "\n";

        out << "srt_byte_rcv_buf"
            << labels << " "
            << stats.byteRcvBuf << "\n";
        /*
         * Interval-based statistics
         */

        out << "srt_pkt_sent"
            << labels << " "
            << stats.pktSent << "\n";

        out << "srt_pkt_recv"
            << labels << " "
            << stats.pktRecv << "\n";

        out << "srt_pkt_sent_unique"
            << labels << " "
            << stats.pktSentUnique << "\n";

        out << "srt_pkt_recv_unique"
            << labels << " "
            << stats.pktRecvUnique << "\n";

        out << "srt_pkt_snd_loss"
            << labels << " "
            << stats.pktSndLoss << "\n";

        out << "srt_pkt_rcv_loss"
            << labels << " "
            << stats.pktRcvLoss << "\n";

        out << "srt_pkt_retrans"
            << labels << " "
            << stats.pktRetrans << "\n";

        out << "srt_pkt_rcv_retrans"
            << labels << " "
            << stats.pktRcvRetrans << "\n";

        out << "srt_pkt_sent_ack"
            << labels << " "
            << stats.pktSentACK << "\n";

        out << "srt_pkt_recv_ack"
            << labels << " "
            << stats.pktRecvACK << "\n";

        out << "srt_pkt_sent_nak"
            << labels << " "
            << stats.pktSentNAK << "\n";

        out << "srt_pkt_recv_nak"
            << labels << " "
            << stats.pktRecvNAK << "\n";

        out << "srt_mbps_send_rate"
            << labels << " "
            << stats.mbpsSendRate << "\n";

        out << "srt_mbps_recv_rate"
            << labels << " "
            << stats.mbpsRecvRate << "\n";

        out << "srt_us_snd_duration"
            << labels << " "
            << stats.usSndDuration << "\n";

        out << "srt_pkt_reorder_distance"
            << labels << " "
            << stats.pktReorderDistance << "\n";

        out << "srt_ms_rcv_avg_belated_time"
            << labels << " "
            << stats.pktRcvAvgBelatedTime << "\n";

        out << "srt_pkt_rcv_belated"
            << labels << " "
            << stats.pktRcvBelated << "\n";

        out << "srt_pkt_snd_drop"
            << labels << " "
            << stats.pktSndDrop << "\n";

        out << "srt_pkt_rcv_drop"
            << labels << " "
            << stats.pktRcvDrop << "\n";

        out << "srt_pkt_rcv_undecrypt"
            << labels << " "
            << stats.pktRcvUndecrypt << "\n";

        out << "srt_byte_sent"
            << labels << " "
            << stats.byteSent << "\n";

        out << "srt_byte_recv"
            << labels << " "
            << stats.byteRecv << "\n";

        out << "srt_byte_sent_unique"
            << labels << " "
            << stats.byteSentUnique << "\n";

        out << "srt_byte_recv_unique"
            << labels << " "
            << stats.byteRecvUnique << "\n";

        out << "srt_byte_rcv_loss"
            << labels << " "
            << stats.byteRcvLoss << "\n";

        out << "srt_byte_retrans"
            << labels << " "
            << stats.byteRetrans << "\n";

        out << "srt_byte_snd_drop"
            << labels << " "
            << stats.byteSndDrop << "\n";

        out << "srt_byte_rcv_drop"
            << labels << " "
            << stats.byteRcvDrop << "\n";

        out << "srt_byte_rcv_undecrypt"
            << labels << " "
            << stats.byteRcvUndecrypt << "\n";

        out << "srt_pkt_snd_filter_extra"
            << labels << " "
            << stats.pktSndFilterExtra << "\n";

        out << "srt_pkt_rcv_filter_extra"
            << labels << " "
            << stats.pktRcvFilterExtra << "\n";

        out << "srt_pkt_rcv_filter_supply"
            << labels << " "
            << stats.pktRcvFilterSupply << "\n";

        out << "srt_pkt_rcv_filter_loss"
            << labels << " "
            << stats.pktRcvFilterLoss << "\n";
    }

    return out.str();
}

void exporter::start()
{
    if (m_thread.joinable())
        return;

    m_server.reset(new httplib::Server());
/*
 * cpp-httplib enables SO_REUSEPORT by default on Linux.
 *
 * This would allow multiple srt-xtransmit processes to listen on the
 * same Prometheus TCP port. Incoming scrapes could then be distributed
 * between different processes.
 *
 * For a Prometheus exporter we want an exclusive TCP listener instead.
 */
m_server->set_socket_options(
    [](socket_t sock)
{
        httplib::set_socket_opt(
            sock,
            SOL_SOCKET,
            SO_REUSEADDR,
            1);

#ifdef SO_REUSEPORT
        httplib::set_socket_opt(
            sock,
            SOL_SOCKET,
            SO_REUSEPORT,
            0);
#endif
    });

    m_server->Get(
    "/metrics",
    [this](const httplib::Request&,
           httplib::Response& res)
    {
        res.set_content(
            render_metrics(),
            "text/plain; version=0.0.4; charset=utf-8");
    });

    /*
     * Bind synchronously.
     *
     * This is intentional: if the TCP port is already occupied,
     * we want srt-xtransmit to fail immediately instead of silently
     * starting without a Prometheus endpoint.
     */
    if (!m_server->bind_to_port("0.0.0.0", m_port))
    {
        m_server.reset();

        throw std::runtime_error(
            "Failed to bind Prometheus exporter TCP port " +
            std::to_string(m_port));
    }

    spdlog::info(
        "PROMETHEUS Listening on TCP port {} (/metrics)",
        m_port);

    /*
     * listen_after_bind() blocks, therefore run it in its own thread.
     */
    m_thread = std::thread(
        [this]()
        {
            if (!m_server->listen_after_bind())
            {
                spdlog::error(
                    "PROMETHEUS HTTP server stopped unexpectedly "
                    "on TCP port {}",
                    m_port);
            }
        });
}


void exporter::stop()
{
    if (m_server)
        m_server->stop();

    if (m_thread.joinable())
        m_thread.join();

    m_server.reset();
}

} // namespace prometheus
} // namespace xtransmit
