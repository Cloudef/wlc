#ifndef _WLC_COMPOSITOR_H_
#define _WLC_COMPOSTIOR_H_

#include <stdbool.h>
#include <wayland-util.h>

struct wl_display;
struct wl_event_loop;
struct wl_event_source;
struct wlc_shell;
struct wlc_xdg_shell;
struct wlc_context;
struct wlc_render;

struct wlc_compositor {
   struct wl_display *display;
   struct wl_event_loop *event_loop;
   struct wlc_shell *shell;
   struct wlc_xdg_shell *xdg_shell;
   struct wlc_context *context;
   struct wlc_render *render;

   struct wl_list surfaces;
   struct wl_event_source *finish_frame_timer;

   bool repaint_scheduled;

   struct {
      void (*schedule_repaint)(struct wlc_compositor *compositor);
   } api;
};

void wlc_compositor_run(struct wlc_compositor *compositor);
void wlc_compositor_free(struct wlc_compositor *compositor);
struct wlc_compositor* wlc_compositor_new(void);

#endif /* _WLC_COMPOSITOR_H_ */
