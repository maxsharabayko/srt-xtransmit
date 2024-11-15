#pragma once
#include "srt.h"

#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION(1, 5, 0)
#include "threadname.h" // srt::ThreadName
#define XTR_THREADNAME(name) srt::ThreadName tn(name);
#else
#define XTR_THREADNAME(name)
#endif

namespace xtransmit
{
namespace details
{
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
#if defined(_MSC_VER) || __cplusplus >= 201402L // C++14 and beyond
	return std::make_unique<T>(std::forward<Args>(args)...);
#else
	static_assert(!std::is_array<T>::value, "arrays are not supported");
	return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
#endif
}
} // namespace details
} // namespace xtransmit
