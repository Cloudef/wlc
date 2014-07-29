#ifndef _WLC_XDG_SHELL_H_
#define _WLC_XDG_SHELL_H_

struct wlc_compositor;

struct wlc_xdg_shell {
   struct wl_global *global;
   struct wlc_compositor *compositor;
};

void wlc_xdg_shell_free(struct wlc_xdg_shell *xdg_shell);
struct wlc_xdg_shell* wlc_xdg_shell_new(struct wlc_compositor *compositor);

#endif /* _WLC_XDG_SHELL_H_ */
