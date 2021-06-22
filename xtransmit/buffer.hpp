#pragma once
#include <cstddef>

namespace xtransmit
{

/// Kindly borrowed from Boost

/// Holds a buffer that can be modified.
/**
 * The mutable_buffer class provides a safe representation of a buffer that can
 * be modified. It does not own the underlying data, and so is cheap to copy or
 * assign.
 *
 * @par Accessing Buffer Contents
 *
 * The contents of a buffer may be accessed using the @c data() and @c size()
 * member functions:
 *
 * @code mutable_buffer b1 = ...;
 * std::size_t s1 = b1.size();
 * unsigned char* p1 = static_cast<unsigned char*>(b1.data());
 * @endcode
 *
 * The @c data() member function permits violations of type safety, so uses of
 * it in application code should be carefully considered.
 */
class mutable_buffer
{
  public:
	/// Construct an empty buffer.
	mutable_buffer() noexcept : data_(nullptr), size_(0) {}

	/// Construct a buffer to represent a given memory range.
	mutable_buffer(void *data, std::size_t size) noexcept : data_(data), size_(size) {}

	/// Get a pointer to the beginning of the memory range.
	void *data() const noexcept
	{
		return data_;
	}

	/// Get the size of the memory range.
	std::size_t size() const noexcept { return size_; }

	/// Move the start of the buffer by the specified number of bytes.
	mutable_buffer &operator+=(std::size_t n) noexcept
	{
		std::size_t offset = n < size_ ? n : size_;
		data_              = static_cast<char *>(data_) + offset;
		size_             -= offset;
		return *this;
	}

  private:
	void *      data_;
	std::size_t size_;
};




/// Holds a buffer that cannot be modified.
/**
 * The const_buffer class provides a safe representation of a buffer that cannot
 * be modified. It does not own the underlying data, and so is cheap to copy or
 * assign.
 *
 * @par Accessing Buffer Contents
 *
 * The contents of a buffer may be accessed using the @c data() and @c size()
 * member functions:
 *
 * @code const_buffer b1 = ...;
 * std::size_t s1 = b1.size();
 * const unsigned char* p1 = static_cast<const unsigned char*>(b1.data());
 * @endcode
 *
 * The @c data() member function permits violations of type safety, so uses of
 * it in application code should be carefully considered.
 */
class const_buffer
{
  public:
	/// Construct an empty buffer.
	const_buffer() noexcept
	    : data_(nullptr)
	    , size_(0)
	{
	}

	/// Construct a buffer to represent a given memory range.
	const_buffer(const void *data, std::size_t size) noexcept
	    : data_(data)
	    , size_(size)
	{
	}

	/// Construct a non-modifiable buffer from a modifiable one.
	const_buffer(const mutable_buffer &b) noexcept
	    : data_(b.data())
	    , size_(b.size())
	{
	}

	/// Get a pointer to the beginning of the memory range.
	const void *data() const noexcept
	{
		return data_;
	}

	/// Get the size of the memory range.
	std::size_t size() const noexcept { return size_; }

	/// Move the start of the buffer by the specified number of bytes.
	const_buffer &operator+=(std::size_t n) noexcept
	{
		std::size_t offset = n < size_ ? n : size_;
		data_              = static_cast<const char *>(data_) + offset;
		size_ -= offset;
		return *this;
	}

  private:
	const void *data_;
	std::size_t size_;
};



} // namespace xtransmit
