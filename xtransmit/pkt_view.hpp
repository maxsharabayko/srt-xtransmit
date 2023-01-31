#pragma once
#include <iostream>
#include "bits.hpp"

namespace srtx
{

/// Packet field description.
/// @tparam ValueType defines the value of the field, e.g. uint32_t, uint8_t.
/// @tparam ByteOffset defines the offset to the field in bytes.
template <typename ValueType, ptrdiff_t ByteOffset>
struct pkt_field
{
	static constexpr ptrdiff_t offset = ByteOffset;
	typedef ValueType          type;
};

/// Packet view class provides read and write operations
/// on a buffer, holding packet data, not owning the buffer itself.
/// It provides a view on the buffer as an SRT packet.
/// @details
/// Usage: Use with <tt>const_buffer</tt> or <tt>mutable_buffer</tt>.
///
/// @tparam StorageType can be either mutable_buffer or const_buffer
///
template <typename StorageType>
class pkt_view
{
public:
	pkt_view()
		: view_()
		, len_(0)
	{
	}

	pkt_view(const StorageType &buffer)
		: view_(buffer)
		, len_(buffer.size())
	{
	}

	pkt_view(const StorageType &&buffer)
		: view_(buffer)
		, len_(buffer.size())
	{
	}

	pkt_view(const StorageType &&buffer, size_t len)
		: view_(buffer)
		, len_(len)
	{
	}

	pkt_view(const pkt_view &other)
		: view_(other.view_)
		, len_(other.len_)
	{
	}

	pkt_view(const pkt_view &&other)
		: view_(other.view_)
		, len_(other.len_)
	{
	}

	pkt_view &operator=(const pkt_view &other)
	{
		this->view_ = other.view_;
		this->len_  = other.len_;
		return *this;
	}

public:
	/// Get the field value.
	template <class field_desc>
	typename field_desc::type get_field() const
	{
		using valtype      = typename field_desc::type;
		const valtype *ptr = reinterpret_cast<const valtype *>(view_.data()) + field_desc::offset / sizeof(valtype);
		return bswap<valtype>(*ptr);
	}

	/// Get the field value.
	template <typename T>
	T get_field(ptrdiff_t byte_offset) const
	{
		const T *ptr = reinterpret_cast<const T *>(at(byte_offset));
		return bswap<T>(*ptr);
	}

	const uint8_t *at(ptrdiff_t byte_offset) const
	{
		const uint8_t *ptr = reinterpret_cast<const uint8_t *>(view_.data()) + byte_offset;
		return ptr;
	}

	StorageType slice(ptrdiff_t byte_offset) const noexcept { return this->view_.slice(byte_offset); }

	size_t length() const { return this->len_; }
	size_t capacity() const { return this->view_.size(); }

protected:
	StorageType view_; ///< buffer view
	size_t      len_;  ///< actual length of content
};

} // namespace srtx
