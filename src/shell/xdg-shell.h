#ifndef _WLC_XDG_SHELL_H_
#define _WLC_XDG_SHELL_H_

struct wl_display;

struct wlc_xdg_shell {
   void *user_data;
};

void wlc_xdg_shell_free(struct wlc_xdg_shell *xdg_shell);
struct wlc_xdg_shell* wlc_xdg_shell_new(struct wl_display *display, void *user_data);

#endif /* _WLC_XDG_SHELL_H_ */
