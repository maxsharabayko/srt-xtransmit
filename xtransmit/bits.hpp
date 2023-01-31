#pragma once

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__)
#include <sys/endian.h>
#define bswap_16 bswap16
#define bswap_32 bswap32
#define bswap_64 bswap64
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define bswap_16 OSSwapInt16
#define bswap_32 OSSwapInt32
#define bswap_64 OSSwapInt64
#elif defined(WIN32)
#include <stdlib.h>
#define bswap_16 _byteswap_ushort
#define bswap_32 _byteswap_ulong
#define bswap_64 _byteswap_uint64
#else
#include <byteswap.h>
#endif

namespace srtx
{
template <typename T>
T bswap(T val);

template <>
inline uint32_t bswap(uint32_t val)
{
	return bswap_32(val);
}

template <>
inline uint16_t bswap(uint16_t val)
{
	return bswap_16(val);
}

template <>
inline uint8_t bswap(uint8_t val)
{
	return val;
}

} // namespace srtx
