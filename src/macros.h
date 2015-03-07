#ifndef _WLC_MACROS_H_
#define _WLC_MACROS_H_

/** fatal stub implementation of wayland interface method. */
#define STUB(x) { \
      wlc_log(WLC_LOG_WARN, "%s @ line %d is not implemented", __PRETTY_FUNCTION__, __LINE__); \
      wl_resource_post_error(x, WL_DISPLAY_ERROR_INVALID_METHOD, "%s @ line %d is not implemented", __PRETTY_FUNCTION__, __LINE__); \
   }

/** non-fatal stub implementation of wayland interface method. */
#define STUBL(x) wlc_log(WLC_LOG_WARN, "%s @ line %d is not implemented", __PRETTY_FUNCTION__, __LINE__)

#ifndef static_assert
#  define static_assert_x(x, y) typedef char static_assertion_##y[(x) ? 1 : -1]
#else
// C11, but we default to C99 for now in cmake
#  define static_assert_x(x, y) static_assert(x, #y)
#endif

#define except(x) if (!(x)) { wlc_log(WLC_LOG_ERROR, "assertion failed: %s", #x); abort(); }

#endif /* _WLC_MACROS_H_ */
