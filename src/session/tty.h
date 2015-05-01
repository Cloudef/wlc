#ifndef _WLC_TTY_H_
#define _WLC_TTY_H_

// Do not call these functions directly!
// Use the fd.h counterparts instead.
bool wlc_tty_activate(void);
bool wlc_tty_deactivate(void);
bool wlc_tty_activate_vt(int vt);

void wlc_tty_setup_signal_handlers(void);
int wlc_tty_get_vt(void);
void wlc_tty_terminate(void);
void wlc_tty_init(int vt);

#endif /* _WLC_TTY_H_ */
