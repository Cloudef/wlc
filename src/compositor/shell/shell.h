#ifndef _WLC_SHELL_H_
#define _WLC_SHELL_H_

struct wlc_compositor;

struct wlc_shell {
   struct wl_global *global;
   struct wlc_compositor *compositor;
};

void wlc_shell_free(struct wlc_shell *shell);
struct wlc_shell* wlc_shell_new(struct wlc_compositor *compositor);

#endif /* _WLC_SHELL_H_ */
