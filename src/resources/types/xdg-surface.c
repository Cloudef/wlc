#include <stdlib.h>
#include <assert.h>
#include "xdg-surface.h"
#include "internal.h"
#include "macros.h"
#include "compositor/view.h"
#include "compositor/output.h"
#include "compositor/seat/seat.h"
#include "compositor/seat/pointer.h"
#include "resources/types/surface.h"

static_assert_x(XDG_SHELL_VERSION_CURRENT == 5, generated_protocol_and_implementation_version_are_different);

static void
xdg_cb_surface_set_parent(struct wl_client *client, struct wl_resource *resource, struct wl_resource *parent_resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   struct wlc_view *parent = (parent_resource ? convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(parent_resource), "view") : NULL);
   wlc_view_set_parent_ptr(view, parent);
}

static void
xdg_cb_surface_set_title(struct wl_client *client, struct wl_resource *resource, const char *title)
{
   (void)client;
   wlc_view_set_title_ptr(convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view"), title);
}

static void
xdg_cb_surface_set_app_id(struct wl_client *client, struct wl_resource *resource, const char *app_id)
{
   (void)client;
   wlc_view_set_app_id_ptr(convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view"), app_id);
}

static void
xdg_cb_surface_show_window_menu(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y)
{
   (void)client, (void)resource, (void)seat, (void)serial, (void)x, (void)y;
   STUBL(resource);
}

static void
xdg_cb_surface_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial)
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
xdg_cb_surface_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, uint32_t edges)
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
xdg_cb_surface_ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
   (void)client, (void)serial;

   // XXX: Some clients such simple-damage from weston does not trigger this
#if 0
   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   view->state.ack = ACK_NEXT_COMMIT;
#else
   (void)resource;
#endif
}

static void
xdg_cb_surface_set_window_geometry(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   view->pending.visible = (struct wlc_geometry){ { x, y }, { width, height } };
}

static void
xdg_cb_surface_set_maximized(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   wlc_view_request_state(view, WLC_BIT_MAXIMIZED, true);
}

static void
xdg_cb_surface_unset_maximized(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   wlc_view_request_state(view, WLC_BIT_MAXIMIZED, false);
}

static void
xdg_cb_surface_set_fullscreen(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   struct wlc_output *output;
   if (output_resource && ((output = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(output_resource), "output"))))
      wlc_view_set_output_ptr(view, output);

   wlc_view_request_state(view, WLC_BIT_FULLSCREEN, true);
}

static void
xdg_cb_surface_unset_fullscreen(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   wlc_view_request_state(view, WLC_BIT_FULLSCREEN, false);
}

static void
xdg_cb_surface_set_minimized(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   wlc_view_set_minimized_ptr(convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view"), true);
}

const struct xdg_surface_interface xdg_surface_implementation = {
   .destroy = wlc_cb_resource_destructor,
   .set_parent = xdg_cb_surface_set_parent,
   .set_title = xdg_cb_surface_set_title,
   .set_app_id = xdg_cb_surface_set_app_id,
   .show_window_menu = xdg_cb_surface_show_window_menu,
   .move = xdg_cb_surface_move,
   .resize = xdg_cb_surface_resize,
   .ack_configure = xdg_cb_surface_ack_configure,
   .set_window_geometry = xdg_cb_surface_set_window_geometry,
   .set_maximized = xdg_cb_surface_set_maximized,
   .unset_maximized = xdg_cb_surface_unset_maximized,
   .set_fullscreen = xdg_cb_surface_set_fullscreen,
   .unset_fullscreen = xdg_cb_surface_unset_fullscreen,
   .set_minimized = xdg_cb_surface_set_minimized
};
