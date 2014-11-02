#ifndef _WLC_COMPOSITOR_H_
#define _WLC_COMPOSITOR_H_

#include "wlc.h"
#include <stdbool.h>
#include <wayland-util.h>

struct wl_display;
struct wl_event_loop;
struct wl_event_source;
struct wlc_data_device_manager;
struct wlc_seat;
struct wlc_shell;
struct wlc_output;
struct wlc_xdg_shell;
struct wlc_backend;
struct wlc_context;
struct wlc_render;

struct wlc_compositor {
   struct wl_global *global, *global_sub;
   struct wl_display *display;
   struct wl_event_loop *event_loop;
   struct wlc_data_device_manager *manager;
   struct wlc_seat *seat;
   struct wlc_shell *shell;
   struct wlc_xdg_shell *xdg_shell;
   struct wlc_backend *backend;
   struct wlc_output *output;
   struct wlc_interface interface;

   struct wl_list clients, outputs;

   struct {
      bool (*add_output)(struct wlc_compositor *compositor, struct wlc_output *output);
      void (*remove_output)(struct wlc_compositor *compositor, struct wlc_output *output);
      void (*active_output)(struct wlc_compositor *compositor, struct wlc_output *output);
   } api;

   struct {
      // XXX: temporary
      bool enable_bg;
   } options;

   bool terminating;
};

#endif /* _WLC_COMPOSITOR_H_ */
