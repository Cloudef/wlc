#include <stdlib.h>
#include <assert.h>
#include "xdg-toplevel.h"
#include "internal.h"
#include "macros.h"
#include "compositor/view.h"
#include "compositor/output.h"
#include "compositor/seat/seat.h"
#include "compositor/seat/pointer.h"
#include "resources/types/surface.h"

static void
xdg_cb_toplevel_set_parent(struct wl_client *client, struct wl_resource *resource, struct wl_resource *parent_resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   struct wlc_view *parent = (parent_resource ? convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(parent_resource), "view") : NULL);
   wlc_view_set_parent_ptr(view, parent);
}

static void
xdg_cb_toplevel_set_title(struct wl_client *client, struct wl_resource *resource, const char *title)
{
   (void)client;
   wlc_view_set_title_ptr(convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view"), title, strlen(title));
}

static void
xdg_cb_toplevel_set_app_id(struct wl_client *client, struct wl_resource *resource, const char *app_id)
{
   (void)client;
   wlc_view_set_app_id_ptr(convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view"), app_id);
}

static void
xdg_cb_toplevel_show_window_menu(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y)
{
   (void)client, (void)resource, (void)seat, (void)serial, (void)x, (void)y;
   STUBL(resource);
}

static void
xdg_cb_toplevel_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial)
{
   (void)client, (void)resource, (void)serial;

   struct wlc_seat *seat;
   if (!(seat = wl_resource_get_user_data(seat_resource)))
      return;

   if (!seat->pointer.focused.view)
      return;

   wlc_dlog(WLC_DBG_REQUEST, "(%" PRIuWLC ") requested move", seat->pointer.focused.view);
   const struct wlc_point o = { seat->pointer.pos.x, seat->pointer.pos.y };
   WLC_INTERFACE_EMIT(view.request.move, seat->pointer.focused.view, &o);
}

static void
xdg_cb_toplevel_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, uint32_t edges)
{
   (void)client, (void)resource, (void)serial;

   struct wlc_seat *seat;
   if (!(seat = wl_resource_get_user_data(seat_resource)))
      return;

   if (!seat->pointer.focused.view)
      return;

   wlc_dlog(WLC_DBG_REQUEST, "(%" PRIuWLC ") requested resize", seat->pointer.focused.view);
   const struct wlc_point o = { seat->pointer.pos.x, seat->pointer.pos.y };
   WLC_INTERFACE_EMIT(view.request.resize, seat->pointer.focused.view, edges, &o);
}

static void
xdg_cb_toplevel_set_max_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height)
{
   (void)client, (void)resource, (void)width, (void)height;
}

static void
xdg_cb_toplevel_set_min_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height)
{
   (void)client, (void)resource, (void)width, (void)height;
}

static void
xdg_cb_toplevel_set_maximized(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   wlc_view_request_state(view, WLC_BIT_MAXIMIZED, true);
}

static void
xdg_cb_toplevel_unset_maximized(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   wlc_view_request_state(view, WLC_BIT_MAXIMIZED, false);
}

static void
xdg_cb_toplevel_set_fullscreen(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   if (!wlc_view_request_state(view, WLC_BIT_FULLSCREEN, true))
      return;

   struct wlc_output *output;
   if (output_resource && ((output = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(output_resource), "output"))))
      wlc_view_set_output_ptr(view, output);
}

static void
xdg_cb_toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   wlc_view_request_state(view, WLC_BIT_FULLSCREEN, false);
}

static void
xdg_cb_toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   wlc_view_set_minimized_ptr(convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view"), true);
}

WLC_CONST const struct zxdg_toplevel_v6_interface*
wlc_xdg_toplevel_implementation(void)
{
   static const struct zxdg_toplevel_v6_interface zxdg_toplevel_v6_implementation = {
      .destroy = wlc_cb_resource_destructor,
      .set_parent = xdg_cb_toplevel_set_parent,
      .set_title = xdg_cb_toplevel_set_title,
      .set_app_id = xdg_cb_toplevel_set_app_id,
      .show_window_menu = xdg_cb_toplevel_show_window_menu,
      .move = xdg_cb_toplevel_move,
      .resize = xdg_cb_toplevel_resize,
      .set_max_size = xdg_cb_toplevel_set_max_size,
      .set_min_size = xdg_cb_toplevel_set_min_size,
      .set_maximized = xdg_cb_toplevel_set_maximized,
      .unset_maximized = xdg_cb_toplevel_unset_maximized,
      .set_fullscreen = xdg_cb_toplevel_set_fullscreen,
      .unset_fullscreen = xdg_cb_toplevel_unset_fullscreen,
      .set_minimized = xdg_cb_toplevel_set_minimized
   };

   return &zxdg_toplevel_v6_implementation;
}
