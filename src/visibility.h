#ifndef _WLC_VISIBLITY_H_
#define _WLC_VISIBLITY_H_

#if defined _WIN32 || defined __CYGWIN__
#  define WLC_HELPER_DLL_EXPORT __declspec(dllexport)
#else
#  if __GNUC__ >= 4
#     define WLC_HELPER_DLL_EXPORT __attribute__((visibility("default")))
#  else
#     define WLC_HELPER_DLL_EXPORT
#  endif
#endif

#ifdef WLC_BUILD_SHARED
#  define WLC_API WLC_HELPER_DLL_EXPORT
#else
#  define WLC_API
#endif

#endif /* _WLC_VISIBILITY_H_ */
