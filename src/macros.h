#ifndef _WLC_MACROS_H_
#define _WLC_MACROS_H_

#ifndef MIN
#  define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#endif

#define STUB(x) { \
      wlc_log(WLC_LOG_WARN, "%s @ line %d is not implemented\n", __PRETTY_FUNCTION__, __LINE__); \
      wl_resource_post_error(x, WL_DISPLAY_ERROR_INVALID_METHOD, "%s @ line %d is not implemented", __PRETTY_FUNCTION__, __LINE__); \
   }

#define STUBL(x) wlc_log(WLC_LOG_WARN, "%s @ line %d is not implemented\n", __PRETTY_FUNCTION__, __LINE__)

#endif /* _WLC_MACROS_H_ */
