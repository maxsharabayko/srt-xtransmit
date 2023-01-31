#pragma once

#include "pkt_view.hpp"

namespace srtx
{

namespace packet
{
	static const uint32_t MAX_TIMESTAMP = 0xFFFFFFFF; // Full 32 bit (HH:MM:SS 01:11:35)
	static const uint32_t MAX_SIZE_PKT_SHUTWOWN = 16;
	static const uint32_t MAX_SIZE_PKT_KEEPALIVE = 16;
}

enum class ctrl_type
{
	INVALID   = -1, //< invalid control packet type
	HANDSHAKE = 0, //< Connection Handshake. Control: see @a CHandShake.
	KEEPALIVE = 1, //< Keep-alive.
	ACK = 2, //< Acknowledgement. Control: past-the-end sequence number up to which packets have been received.
	LOSSREPORT = 3, //< Negative Acknowledgement (NAK). Control: Loss list.
	CGWARNING = 4, //< Congestion warning.
	SHUTDOWN = 5, //< Shutdown.
	ACKACK = 6, //< Acknowledgement of Acknowledgement. Add info: The ACK sequence number
	DROPREQ = 7, //< Message Drop Request. Add info: Message ID. Control Info: (first, last) number of the message.
	PEERERROR = 8, //< Signal from the Peer side. Add info: Error code.
	USERDEFINED = 0x7FFF //< For the use of user-defined control packets.
};

const char* ctrl_type_str(ctrl_type type);

constexpr size_t SRT_HDR_LENGTH = 16;	// SRT Header length

/// A base class for SRT packet (data or control).
/// All other packet types that have a view to SRT header
/// derive from this class.
template <class storage>
class pkt_base : public pkt_view<storage>
{
public:
	pkt_base()
		: pkt_view<storage>(storage(nullptr, 0))
	{
	}

	pkt_base(const pkt_view<storage> &view)
		: pkt_view<storage>(view)
	{
	}

	pkt_base(const storage &s)
		: pkt_view<storage>(s)
	{
	}


	//  0                   1                   2                   3
	//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	// +-+-+-+-+-+-+-+-+-+-+-+-+- SRT Header +-+-+-+-+-+-+-+-+-+-+-+-+-+
	// |F|        (Field meaning depends on the packet type)           |
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// |          (Field meaning depends on the packet type)           |
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// |                           Timestamp                           |
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// |                     Destination Socket ID                     |
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+- CIF -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// +                        Packet Contents                        +
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	typedef pkt_field<uint16_t, 0>  fld_ctrltype;
	typedef pkt_field<uint16_t, 2>  fld_subtype;
	typedef pkt_field<uint32_t, 8>  fld_timestamp;
	typedef pkt_field<uint32_t, 12> fld_dstsockid;

public:
	bool is_ctrl() const { return (0x80 & (pkt_view<storage>::template get_field<pkt_field<uint8_t, 0>>())) != 0; }
	bool is_data() const { return !this->is_ctrl(); }

	ctrl_type control_type() const;
	uint16_t subtype() const { return pkt_view<storage>::template get_field<fld_subtype>(); };

	/// Get packet timestamp.
	/// @note A dependant template definition depends on template parameter,
	/// therefore the template keyword is required (ISO C++03 14.2/4).
	uint32_t timestamp() const { return pkt_view<storage>::template get_field<fld_timestamp>(); }

	/// Get destination socket ID.
	uint32_t dstsockid() const { return pkt_view<storage>::template get_field<fld_dstsockid>(); }
};

template<class storage>
inline ctrl_type pkt_base<storage>::control_type() const
{
	// Discard the highest bit, which defines type of packet: ctrl/data
	const uint16_t type = 0x7FFF & pkt_view<storage>::template get_field<fld_ctrltype>();
	if ((type >= (uint16_t)ctrl_type::HANDSHAKE && type <= (uint16_t)ctrl_type::PEERERROR)
		|| type == (uint16_t)ctrl_type::USERDEFINED)
	{
		return static_cast<ctrl_type>(type);
	}

	return ctrl_type::INVALID;
}

} // namespace srtx
