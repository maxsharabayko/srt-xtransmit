/*****************************************************************************
 SRTX Project.
 Copyright (c) 2023 Haivision Systems Inc.
 This Source Code Form is subject to the terms of the Mozilla Public
 License, v. 2.0. If a copy of the MPL was not distributed with this
 file, You can obtain one at http://mozilla.org/MPL/2.0/.

 Authors:
   Maxim Sharabayko
*****************************************************************************/
#pragma once

#include "pkt_base.hpp"

namespace srtx
{

	namespace pkt::flags
	{
		/// Packet Position Flag (PP): SRT data packet flags indicating packet position in a message.
		enum class pp : uint8_t
		{
			/// 00: middle packet of a message.
			middle = 0,
			/// 01: last packet of a message.
			last = 1,
			/// 10: first packet of a message.
			first = 2,
			/// 11: single packet message.
			single = first | last
		};

		/// Key-based Encryption (KK) Flag idicating if the payload is encrypted or not
		/// or, in case of a KM message, which key(s) are wrapped.
		enum class kk : uint8_t
		{
			no = 0, // no encryption.
			even = 1, // encrypted with even key.
			odd = 2, // encrypted with odd key.
			both = 3  // both even and odd (for KM messages).
		};

	} // namespace pkt::flags

	namespace pkt
	{
		inline bool pp_is_first(flags::pp v)
		{
			using T = std::underlying_type<flags::pp>::type;
			return (static_cast<T>(v) & static_cast<T>(flags::pp::first));
		}

		inline bool pp_is_last(flags::pp v)
		{
			using T = std::underlying_type<flags::pp>::type;
			return (static_cast<T>(v) & static_cast<T>(flags::pp::last));
		}
		inline bool pp_is_single(flags::pp v) { return v == flags::pp::single; }
		inline bool pp_is_middle(flags::pp v) { return v == flags::pp::middle; }

		constexpr uint32_t MSGNO_BITS = 26;
		//typedef roll_number<MSGNO_BITS, 1> msgno_t;

		constexpr size_t SRT_HDR_SIZE = 16; // 4 x 32-bit fields
		constexpr size_t UDP_HDR_SIZE = 28; // 20 bytes IPv4 + 8 bytes of UDP { u16 sport, dport, len, csum }.
		constexpr size_t SRT_DATA_HDR_SIZE = UDP_HDR_SIZE + SRT_HDR_SIZE;

	} // namespace pkt

	/// Class for SRT DATA packet header (w/o payload).
	template <class storage>
	class pkt_data_hdr : public pkt_base<storage>
	{
	public:
		pkt_data_hdr()
			: pkt_base<storage>()
		{
		}

		pkt_data_hdr(const storage& buf_view)
			: pkt_base<storage>(buf_view)
		{
		}

		pkt_data_hdr(const pkt_base<storage>& pkt)
			: pkt_base<storage>(pkt)
		{
		}


	public:
		/// SRT header of a data packet:
		///    0                   1                   2                   3
		///    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		///   +-+-+-+-+-+-+-+-+-+-+-+-+- SRT Header +-+-+-+-+-+-+-+-+-+-+-+-+-+
		/// 0 |0|                    Packet Sequence Number                   | [0; 2^31]
		///   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		/// 1 |P P|O|K K|R|                   Message Number                  | [1; 2^26]
		///   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		/// 2 |                           Timestamp                           |
		///   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		/// 3 |                     Destination Socket ID                     |
		///   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		static constexpr ptrdiff_t OFFSET_SEQNO = 0;
		static constexpr ptrdiff_t OFFSET_KK = 4;

		using fld_seqno = pkt_field<uint32_t, OFFSET_SEQNO>;
		using fld_kk = pkt_field<uint8_t, OFFSET_KK>;

	public: // Getters
		int32_t seqno() const { return (int32_t)pkt_view<storage>::template get_field<fld_seqno>(); }
		uint32_t msgno() const
		{
			// Discard six higher bits
			typedef pkt_field<uint32_t, 1 * 4> fld_msgno;
			return 0x03FFFFFF & pkt_view<storage>::template get_field<fld_msgno>();
		}

		/// Packet Position (PP) flags.
		pkt::flags::pp position_flags() const
		{
			typedef pkt_field<uint8_t, 4> fld_pb;
			return (pkt::flags::pp)(pkt_view<storage>::template get_field<fld_pb>() >> 6);
		}

		/// Returns true if pp::first or pp::single.
		bool is_pp_first() const
		{
			return pkt::pp_is_first(position_flags());
		}

		/// Returns true if pp::last or pp::single.
		bool is_pp_last() const
		{
			return pkt::pp_is_last(position_flags());
		}

		/// Returns true if pp::single.
		bool is_pp_single() const
		{
			return pkt::pp_is_single(position_flags());
		}

		/// Out-of-order flag defining if the packet must be read in order or can be read out of order.
		bool order_flag() const
		{
			typedef pkt_field<uint8_t, 4> fld_reorder;
			return (0x1 & (pkt_view<storage>::template get_field<fld_reorder>() >> 5)) != 0;
		}

		/// Key-based Encryption (KK) Flag indicating if the payload is encrypted or not (00b).
		/// '01b' - encrypted with an even key; '10b' - encrypted with an odd key.
		unsigned key_flag() const
		{
			return 0x3 & (pkt_view<storage>::template get_field<fld_kk>() >> 3);
		}

		/// The flag indicating if this is an original or a retransmitted packet.
		bool rexmit_flag() const
		{
			typedef pkt_field<uint8_t, 4> fld_rexmit;
			return (0x1 & (pkt_view<storage>::template get_field<fld_rexmit>() >> 2)) != 0;
		}

	public: // Setters
		void seqno(int32_t value) { return pkt_view<storage>::template set_field<fld_seqno>(value); }

		void msgno(uint32_t msgno_val)
		{
			typedef pkt_field<uint32_t, 1 * 4> fld_msgno;
			const uint32_t old_val = pkt_view<storage>::template get_field<fld_msgno>();
			const uint32_t new_val = (old_val & 0xFC000000) | msgno_val;
			return pkt_view<storage>::template set_field<fld_msgno>(new_val);
		}

		/// @brief Set all 32 bits of msgno field, including PP, O, KK and R.
		void msgno_32bits(uint32_t value)
		{
			typedef pkt_field<uint32_t, 1 * 4> fld_msgno;
			return pkt_view<storage>::template set_field<fld_msgno>(value);
		}

		/// @brief Set all 32 bits of msgno fielf, including PP, O, KK and R.
		void msgno_32bits(uint32_t msgno_val, uint32_t pp_bits, uint32_t o_bit, uint32_t kk_bits, uint32_t r_bit)
		{
			typedef pkt_field<uint32_t, 1 * 4> fld_msgno;
			const uint32_t value = msgno_val | (r_bit << 26) | (kk_bits << 27) | (o_bit << 29) | (pp_bits << 30);
			return pkt_view<storage>::template set_field<fld_msgno>(value);
		}

		void key_flag(uint8_t val)
		{
			//SRTX_ASSERT((val & 0xFC) == 0);
			const uint8_t old_val = 0xE7 & (pkt_view<storage>::template get_field<fld_kk>());
			const uint8_t new_val = old_val | (val << 3);
			return pkt_view<storage>::template set_field<fld_kk>(new_val);
		}

		void position_flags(pkt::flags::pp flags);
		void order_flag(bool flag);
	};

	/// Class for SRT DATA packet.
	template <class storage>
	class pkt_data : public pkt_data_hdr<storage>
	{
	public:
		pkt_data()
			: pkt_data_hdr<storage>()
		{
		}

		pkt_data(const storage& buf_view)
			: pkt_data_hdr<storage>(buf_view)
		{
		}

		pkt_data(const pkt_base<storage>& pkt)
			: pkt_data_hdr<storage>(pkt)
		{
		}


	public:
		/// SRT Header:
		///  ... (16 bytes) ...
		/// Payload:
		///   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		/// 4 |                                                               |
		///   +                              Data                             +
		///   |                                                               |
		///   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

	public: // Getters
		const uint8_t* payload() const
		{
			const uint8_t* ptr = this->at(4 * 4);
			return ptr;
		}


		size_t payload_size() const { return this->length() - 4 * 4; }
	};

} // namespace srtx

