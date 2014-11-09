#ifndef _WLC_COMPOSITOR_H_
#define _WLC_COMPOSITOR_H_

#include "wlc.h"
#include <stdbool.h>
#include <wayland-server.h>
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
   void *userdata;
   struct wl_global *global, *global_sub;
   struct wlc_data_device_manager *manager;
   struct wlc_seat *seat;
   struct wlc_shell *shell;
   struct wlc_xdg_shell *xdg_shell;
   struct wlc_backend *backend;
   struct wlc_output *output;
   struct wlc_xwm *xwm;

   struct wl_list clients, outputs;

   struct {
      struct wl_listener activated;
      struct wl_listener terminated;
      struct wl_listener xwayland;
      struct wl_listener output;
   } listener;

   struct {
      // XXX: temporary
      uint32_t idle_time;
      bool enable_bg;
   } options;

   bool terminating;
};

struct wlc_output* wlc_compositor_get_surfaless_output(struct wlc_compositor *compositor);

#endif /* _WLC_COMPOSITOR_H_ */
