#ifndef _WLC_DEFINES_H_
#define _WLC_DEFINES_H_

#ifdef __cplusplus
extern "C" {
#endif 

#include <inttypes.h>

#if __GNUC__
#  define WLC_NONULL __attribute__((nonnull))
#  define WLC_NONULLV(...) __attribute__((nonnull(__VA_ARGS__)))
#  define WLC_PURE __attribute__((pure))
#  define WLC_CONST __attribute__((const))
#  define WLC_DEPRECATED __attribute__((deprecated))
#else
#  define WLC_NONULL
#  define WLC_NONULLV
#  define WLC_PURE
#  define WLC_CONST
#  define WLC_DEPRECATED
#endif

/** printf format specifiers. */
#define PRIoWLC PRIoPTR
#define PRIuWLC PRIuPTR
#define PRIxWLC PRIxPTR
#define PRIXWLC PRIXPTR

typedef uintptr_t wlc_handle;

#ifdef __cplusplus
}
#endif 

#endif /* _WLC_DEFINES_H_ */
