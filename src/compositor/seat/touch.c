#include <string.h>
#include <assert.h>
#include <wayland-server.h>
#include "touch.h"
#include "internal.h"
#include "macros.h"
#include "compositor/compositor.h"
#include "compositor/view.h"
#include "compositor/output.h"

static struct wlc_output*
active_output(struct wlc_touch *touch)
{
   struct wlc_seat *seat;
   struct wlc_compositor *compositor;
   except((seat = wl_container_of(touch, seat, touch)) && (compositor = wl_container_of(seat, compositor, seat)));
   return convert_from_wlc_handle(compositor->active.output, "output");
}

static struct wlc_view*
view_under_touch(const struct wlc_origin *pos, struct wlc_output *output)
{
   assert(pos);

   if (!output)
      return NULL;

   wlc_handle *h;
   chck_iter_pool_for_each_reverse(&output->views, h) {
      struct wlc_view *view;
      if (!(view = convert_from_wlc_handle(*h, "view")))
         continue;

      struct wlc_geometry b;
      wlc_view_get_bounds(view, &b, NULL);
      if (pos->x >= b.origin.x && pos->x <= b.origin.x + (int32_t)b.size.w &&
          pos->y >= b.origin.y && pos->y <= b.origin.y + (int32_t)b.size.h) {
         return view;
      }
   }

   return NULL;
}

void
wlc_touch_touch(struct wlc_touch *touch, uint32_t time, enum wlc_touch_type type, int32_t slot, const struct wlc_origin *pos)
{
   assert(touch);

   struct wlc_view *focused;
   if (!(focused = view_under_touch(pos, active_output(touch))))
      return;

   struct wl_client *client;
   struct wl_resource *focus, *surface;
   if (!(surface = wl_resource_from_wlc_resource(focused->surface, "surface")) ||
       !(client = wl_resource_get_client(surface)) ||
       !(focus = wl_resource_for_client(&touch->resources, client)))
      return;

   switch (type) {
      case WLC_TOUCH_DOWN:
         {
            uint32_t serial = wl_display_next_serial(wlc_display());
            wl_touch_send_down(focus, serial, time, surface, slot, wl_fixed_from_double(pos->x), wl_fixed_from_double(pos->y));
         }
         break;

      case WLC_TOUCH_UP:
         {
            uint32_t serial = wl_display_next_serial(wlc_display());
            wl_touch_send_up(focus, serial, time, slot);
         }
         break;

      case WLC_TOUCH_MOTION:
         wl_touch_send_motion(focus, time, slot, wl_fixed_from_double(pos->x), wl_fixed_from_double(pos->y));
         break;

      case WLC_TOUCH_FRAME:
         wl_touch_send_frame(focus);
         break;

      case WLC_TOUCH_CANCEL:
         wl_touch_send_cancel(focus);
         break;
   }
}

void
wlc_touch_release(struct wlc_touch *touch)
{
   if (!touch)
      return;

   wlc_source_release(&touch->resources);
   memset(touch, 0, sizeof(struct wlc_touch));
}

bool
wlc_touch(struct wlc_touch *touch)
{
   assert(touch);
   memset(touch, 0, sizeof(struct wlc_touch));
   return wlc_source(&touch->resources, "touch", NULL, NULL, 32, sizeof(struct wlc_resource));
}
