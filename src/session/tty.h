#ifndef _WLC_TTY_H_
#define _WLC_TTY_H_

void wlc_tty_setup_signal_handlers(void);
bool wlc_tty_activate_vt(int vt);
void wlc_tty_terminate(void);
void wlc_tty_init(void);

#endif /* _WLC_TTY_H_ */
