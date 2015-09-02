#ifndef _WLC_COMPOSITOR_H_
#define _WLC_COMPOSITOR_H_

#include <wlc/wlc.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include "seat/seat.h"
#include "shell/shell.h"
#include "shell/xdg-shell.h"
#include "xwayland/xwm.h"
#include "resources/resources.h"
#include "platform/backend/backend.h"

struct wlc_view;
struct wlc_surface;

struct wlc_compositor {
   struct wlc_backend backend;
   struct wlc_seat seat;
   struct wlc_shell shell;
   struct wlc_xdg_shell xdg_shell;
   struct wlc_xwm xwm;
   struct wlc_source outputs, views, surfaces, subsurfaces, regions;

   struct {
      wlc_handle output;
   } active;

   struct {
      struct wl_global *compositor;
      struct wl_global *subcompositor;
   } wl;

   struct {
      struct wl_listener activate;
      struct wl_listener terminate;
      struct wl_listener xwayland;
      struct wl_listener surface;
      struct wl_listener output;
      struct wl_listener focus;
   } listener;

   struct {
      wlc_handle *outputs;
   } tmp;

   struct {
      struct wl_event_source *idle;
      enum {
         IDLE,
         ACTIVATING,
         DEACTIVATING,
      } tty;
      int vt;
      bool terminating, ready;
   } state;
};

WLC_NONULL bool wlc_compositor_is_good(struct wlc_compositor *compositor);
WLC_NONULL struct wlc_view* wlc_compositor_view_for_surface(struct wlc_compositor *compositor, struct wlc_surface *surface);
void wlc_compositor_terminate(struct wlc_compositor *compositor);
void wlc_compositor_release(struct wlc_compositor *compositor);
WLC_NONULL bool wlc_compositor(struct wlc_compositor *compositor);

#endif /* _WLC_COMPOSITOR_H_ */
