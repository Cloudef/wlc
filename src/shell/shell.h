#ifndef _WLC_SHELL_H_
#define _WLC_SHELL_H_

struct wl_display;

struct wlc_shell {
   void *user_data;
};

void wlc_shell_free(struct wlc_shell *shell);
struct wlc_shell* wlc_shell_new(struct wl_display *display, void *user_data);

#endif /* _WLC_SHELL_H_ */
