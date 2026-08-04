#pragma once
#include "srt.h"

#if SRT_VERSION_VALUE >= SRT_MAKE_VERSION(1, 5, 0)
#include "threadname.h" // srt::ThreadName
#define XTR_THREADNAME(name) srt::ThreadName tn(name);
#else
#define XTR_THREADNAME(name)
#endif
