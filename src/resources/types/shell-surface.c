#include <stdlib.h>
#include <assert.h>
#include <wayland-server.h>
#include "shell-surface.h"
#include "surface.h"
#include "internal.h"
#include "macros.h"
#include "compositor/view.h"
#include "compositor/output.h"
#include "compositor/seat/seat.h"
#include "compositor/seat/pointer.h"

static void
wl_cb_shell_surface_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
   (void)client, (void)serial;

   struct wlc_view *view;
   struct wlc_surface *surface;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")) ||
       !(surface = convert_from_wlc_resource(view->surface, "surface")))
      return;

   STUBL(resource);
}

static void
wl_cb_shell_surface_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial)
{
   (void)client, (void)resource, (void)serial;

   struct wlc_seat *seat;
   if (!(seat = wl_resource_get_user_data(seat_resource)))
      return;

   if (!seat->pointer.focused.view)
      return;

   const struct wlc_origin o = { seat->pointer.pos.x, seat->pointer.pos.y };
   WLC_INTERFACE_EMIT(view.request.move, seat->pointer.focused.view, &o);
}

static void
wl_cb_shell_surface_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, uint32_t edges)
{
   (void)client, (void)resource, (void)serial;

   struct wlc_seat *seat;
   if (!(seat = wl_resource_get_user_data(seat_resource)))
      return;

   if (!seat->pointer.focused.view)
      return;

   const struct wlc_origin o = { seat->pointer.pos.x, seat->pointer.pos.y };
   WLC_INTERFACE_EMIT(view.request.resize, seat->pointer.focused.view, edges, &o);
}

static void
wl_cb_shell_surface_set_toplevel(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   wlc_view_request_state(view, WLC_BIT_FULLSCREEN, false);
   view->data.fullscreen_mode = WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT;
}

static void
wl_cb_shell_surface_set_transient(struct wl_client *client, struct wl_resource *resource, struct wl_resource *parent_resource, int32_t x, int32_t y, uint32_t flags)
{
   (void)client, (void)flags;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   struct wlc_surface *surface = (parent_resource ? convert_from_wl_resource(parent_resource, "surface") : NULL);
   wlc_view_set_parent_ptr(view, (surface ? convert_from_wlc_handle(surface->view, "view") : NULL));
   view->pending.geometry.origin = (struct wlc_origin){ x, y };
}

static void
wl_cb_shell_surface_set_fullscreen(struct wl_client *client, struct wl_resource *resource, uint32_t method, uint32_t framerate, struct wl_resource *output_resource)
{
   (void)client, (void)method, (void)framerate;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   struct wlc_output *output;
   if (output_resource && ((output = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(output_resource), "output"))))
      wlc_view_set_output_ptr(view, output);

   view->data.fullscreen_mode = method;
   wlc_view_request_state(view, WLC_BIT_FULLSCREEN, true);
}

static void
wl_cb_shell_surface_set_popup(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial, struct wl_resource *parent, int32_t x, int32_t y, uint32_t flags)
{
   (void)client, (void)seat, (void)serial, (void)parent, (void)x, (void)y, (void)flags;
   STUB(resource);
}

static void
wl_cb_shell_surface_set_maximized(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   struct wlc_output *output;
   if (output_resource && ((output = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(output_resource), "output"))))
      wlc_view_set_output_ptr(view, output);

   wlc_view_request_state(view, WLC_BIT_MAXIMIZED, true);
}

static void
wl_cb_shell_surface_set_title(struct wl_client *client, struct wl_resource *resource, const char *title)
{
   (void)client;
   wlc_view_set_title_ptr(convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view"), title);
}

static void
wl_cb_shell_surface_set_class(struct wl_client *client, struct wl_resource *resource, const char *class_)
{
   (void)client;
   wlc_view_set_class_ptr(convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view"), class_);
}

const struct wl_shell_surface_interface wl_shell_surface_implementation = {
   .pong = wl_cb_shell_surface_pong,
   .move = wl_cb_shell_surface_move,
   .resize = wl_cb_shell_surface_resize,
   .set_toplevel = wl_cb_shell_surface_set_toplevel,
   .set_transient = wl_cb_shell_surface_set_transient,
   .set_fullscreen = wl_cb_shell_surface_set_fullscreen,
   .set_popup = wl_cb_shell_surface_set_popup,
   .set_maximized = wl_cb_shell_surface_set_maximized,
   .set_title = wl_cb_shell_surface_set_title,
   .set_class = wl_cb_shell_surface_set_class,
};
