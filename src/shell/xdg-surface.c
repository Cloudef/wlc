#include "xdg-surface.h"
#include "macros.h"

#include "seat/seat.h"
#include "seat/pointer.h"

#include "compositor/view.h"
#include "compositor/output.h"

#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

static void
xdg_cb_surface_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client, (void)resource;
   wl_resource_destroy(resource);
}

static void
xdg_cb_surface_set_parent(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *parent_resource)
{
   (void)wl_client;
   struct wlc_view *view = wl_resource_get_user_data(resource);
   wlc_view_set_parent(view, (parent_resource ? wl_resource_get_user_data(parent_resource) : NULL));
}

static void
xdg_cb_surface_set_title(struct wl_client *wl_client, struct wl_resource *resource, const char *title)
{
   (void)wl_client;
   struct wlc_view *view = wl_resource_get_user_data(resource);
   wlc_view_set_title(view, title);
}

static void
xdg_cb_surface_set_app_id(struct wl_client *wl_client, struct wl_resource *resource, const char *app_id)
{
   (void)wl_client;
   struct wlc_view *view = wl_resource_get_user_data(resource);
   wlc_string_set(&view->xdg_surface.app_id, app_id, true);
}

static void
xdg_cb_surface_show_window_menu(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y)
{
   (void)wl_client, (void)resource, (void)seat, (void)serial, (void)x, (void)y;
   STUBL(resource);
}

static void
xdg_cb_surface_move(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial)
{
   (void)wl_client, (void)resource, (void)serial;
   struct wlc_seat *seat = wl_resource_get_user_data(seat_resource);

   if (!seat->pointer || !seat->pointer->focus)
      return;

   seat->pointer->action = WLC_GRAB_ACTION_MOVE;
}

static void
xdg_cb_surface_resize(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, uint32_t edges)
{
   (void)wl_client, (void)resource, (void)serial, (void)edges;
   struct wlc_seat *seat = wl_resource_get_user_data(seat_resource);

   if (!seat->pointer || !seat->pointer->focus)
      return;

   seat->pointer->action = WLC_GRAB_ACTION_RESIZE;
   seat->pointer->action_edges = edges;
}

static void
xdg_cb_surface_ack_configure(struct wl_client *wl_client, struct wl_resource *resource, uint32_t serial)
{
   (void)wl_client, (void)resource, (void)serial;
   STUBL(resource);
}

static void
xdg_cb_surface_set_window_geometry(struct wl_client *wl_client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)wl_client;
   struct wlc_view *view = wl_resource_get_user_data(resource);
   view->xdg_surface.visible_geometry = (struct wlc_geometry){ { x, y }, { width, height } };
}

static void
xdg_cb_surface_set_maximized(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   struct wlc_view *view = wl_resource_get_user_data(resource);
   wlc_view_request_state(view, WLC_BIT_MAXIMIZED, true);
}

static void
xdg_cb_surface_unset_maximized(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   struct wlc_view *view = wl_resource_get_user_data(resource);
   wlc_view_request_state(view, WLC_BIT_MAXIMIZED, false);
}

static void
xdg_cb_surface_set_fullscreen(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *output_resource)
{
   (void)wl_client;

   struct wlc_view *view = wl_resource_get_user_data(resource);
   struct wlc_output *output = (output_resource ? wl_resource_get_user_data(output_resource) : view->space->output);

   // wlc_view_set_output(view, output);
   wlc_view_request_state(view, WLC_BIT_FULLSCREEN, true);
}

static void
xdg_cb_surface_unset_fullscreen(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   struct wlc_view *view = wl_resource_get_user_data(resource);
   wlc_view_request_state(view, WLC_BIT_FULLSCREEN, false);
}

static void
xdg_cb_surface_set_minimized(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   struct wlc_view *view = wl_resource_get_user_data(resource);
   wlc_xdg_surface_set_minimized(&view->xdg_surface, true);
}

const struct xdg_surface_interface xdg_surface_implementation = {
   xdg_cb_surface_destroy,
   xdg_cb_surface_set_parent,
   xdg_cb_surface_set_title,
   xdg_cb_surface_set_app_id,
   xdg_cb_surface_show_window_menu,
   xdg_cb_surface_move,
   xdg_cb_surface_resize,
   xdg_cb_surface_ack_configure,
   xdg_cb_surface_set_window_geometry,
   xdg_cb_surface_set_maximized,
   xdg_cb_surface_unset_maximized,
   xdg_cb_surface_set_fullscreen,
   xdg_cb_surface_unset_fullscreen,
   xdg_cb_surface_set_minimized
};

static void
xdg_cb_surface_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wlc_view *view = wl_resource_get_user_data(resource);
   view->xdg_surface.resource = NULL;
   wlc_xdg_surface_release(&view->xdg_surface);
}

void
wlc_xdg_surface_implement(struct wlc_xdg_surface *xdg_surface, struct wlc_view *view, struct wl_resource *resource)
{
   assert(xdg_surface);

   if (xdg_surface->resource == resource)
      return;

   if (xdg_surface->resource)
      wl_resource_destroy(xdg_surface->resource);

   xdg_surface->resource = resource;
   wl_resource_set_implementation(xdg_surface->resource, &xdg_surface_implementation, view, xdg_cb_surface_destructor);
}

void
wlc_xdg_surface_set_app_id(struct wlc_xdg_surface *xdg_surface, const char *app_id)
{
   assert(xdg_surface);
   wlc_string_set(&xdg_surface->app_id, app_id, true);
}

void
wlc_xdg_surface_set_minimized(struct wlc_xdg_surface *xdg_surface, bool minimized)
{
   assert(xdg_surface);
   xdg_surface->minimized = minimized;
}

void
wlc_xdg_surface_release(struct wlc_xdg_surface *xdg_surface)
{
   assert(xdg_surface);

   if (xdg_surface->resource)
      wl_resource_destroy(xdg_surface->resource);

   wlc_string_release(&xdg_surface->app_id);
}
