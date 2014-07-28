#ifndef _WLC_GLES2_H_
#define _WLC_GLES2_H_

#include <stdbool.h>

bool wlc_gles2_has_extension(const char *extension);
void wlc_gles2_terminate(void);
bool wlc_gles2_init(void (*swap_func)(void), bool uses_glx);

#endif /* _WLC_GLES2_H_ */
