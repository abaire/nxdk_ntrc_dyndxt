#ifndef NTRC_DYNDXT_SRC_UTIL_PROFILER_H_
#define NTRC_DYNDXT_SRC_UTIL_PROFILER_H_

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ULONGLONG PROFILETOKEN;

PROFILETOKEN ProfileStart(void);

//! Returns the nubmer of milliseconds since the given start_token was
//! initialized via ProfileStart.
double ProfileStop(PROFILETOKEN *start_token);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NTRC_DYNDXT_SRC_UTIL_PROFILER_H_
