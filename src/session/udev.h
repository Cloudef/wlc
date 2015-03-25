#ifndef _WLC_UDEV_H_
#define _WLC_UDEV_H_

#include <stdbool.h>

bool wlc_input_has_init(void);
void wlc_input_terminate(void);
bool wlc_input_init(void);
void wlc_udev_terminate(void);
bool wlc_udev_init(void);

#endif /* _WLC_UDEV_H_ */
