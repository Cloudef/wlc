#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <wayland-server.h>
#include <chck/math/math.h>
#include "internal.h"
#include "session/tty.h"
#include "seat.h"
#include "pointer.h"
#include "keyboard.h"
#include "touch.h"
#include "keymap.h"
#include "macros.h"
#include "compositor/compositor.h"
#include "compositor/output.h"
#include "compositor/view.h"
#include "resources/types/surface.h"
#include "resources/resources.h"

static void
wl_cb_seat_get_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wlc_seat *seat;
   if (!(seat = wl_resource_get_user_data(resource)))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&seat->pointer.resources, client, &wl_pointer_interface, wl_resource_get_version(resource), 4, id)))
      return;

   wlc_resource_implement(r, &wl_pointer_implementation, &seat->pointer);
}

static const struct wl_keyboard_interface wl_keyboard_implementation = {
   .release = wlc_cb_resource_destructor
};

static void
wl_cb_seat_get_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wlc_seat *seat;
   if (!(seat = wl_resource_get_user_data(resource)))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&seat->keyboard.resources, client, &wl_keyboard_interface, wl_resource_get_version(resource), 4, id)))
      return;

   wlc_resource_implement(r, &wl_keyboard_implementation, &seat->keyboard);

   struct wl_resource *wr = wl_resource_from_wlc_resource(r, "keyboard");
   if (wl_resource_get_version(wr) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
      wl_keyboard_send_repeat_info(wr, seat->keyboard.repeat.rate, seat->keyboard.repeat.delay);

   wl_keyboard_send_keymap(wr, seat->keymap.format, seat->keymap.fd, seat->keymap.size);

   struct wlc_view *focused = convert_from_wlc_handle(seat->keyboard.focused.view, "view");
   if (focused && wlc_view_get_client(focused) == client) {
      // We refocus the client here so it gets input correctly.
      // This way we avoid the ugly keyboard.init public interface hack.
      seat->keyboard.focused.view = 0;
      wlc_view_focus_ptr(focused);
   }
}

static const struct wl_touch_interface wl_touch_implementation = {
   .release = wlc_cb_resource_destructor
};

static void
wl_cb_seat_get_touch(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wlc_seat *seat;
   if (!(seat = wl_resource_get_user_data(resource)))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&seat->keyboard.resources, client, &wl_touch_interface, wl_resource_get_version(resource), 4, id)))
      return;

   wlc_resource_implement(r, &wl_touch_implementation, &seat->touch);
}

static const struct wl_seat_interface wl_seat_implementation = {
   .get_pointer = wl_cb_seat_get_pointer,
   .get_keyboard = wl_cb_seat_get_keyboard,
   .get_touch = wl_cb_seat_get_touch
};

static void
wl_seat_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create_checked(client, &wl_seat_interface, version, 4, id)))
      return;

   wl_resource_set_implementation(resource, &wl_seat_implementation, data, NULL);

   // FIXME: need to check if this is supposed to be availability of device or that the seat can handle all the devices if plugged.
   const enum wl_seat_capability caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH | WL_SEAT_CAPABILITY_KEYBOARD;
   wl_seat_send_capabilities(resource, caps);

   if (version >= 2) {
      const char *xdg_seat = getenv("XDG_SEAT");
      wl_seat_send_name(resource, (xdg_seat ? xdg_seat : "seat0"));
   }
}

static void
seat_handle_key(struct wlc_seat *seat, const struct wlc_input_event *ev)
{
   if (!wlc_keyboard_update(&seat->keyboard, ev->key.code, ev->key.state))
      return;

   wlc_keyboard_update_modifiers(&seat->keyboard);

   if (seat->keyboard.modifiers.mods == (WLC_BIT_MOD_CTRL | WLC_BIT_MOD_ALT) && ev->key.code >= 59 && ev->key.code <= 88) {
      const int vt = (ev->key.code - 59) + 1;
      if (ev->key.state == WL_KEYBOARD_KEY_STATE_PRESSED && wlc_tty_get_vt() != vt) {
         struct wlc_activate_event aev = { .active = false, .vt = vt };
         wl_signal_emit(&wlc_system_signals()->activate, &aev);
      }
      return;
   }

   if (!wlc_keyboard_request_key(&seat->keyboard, ev->time, &seat->keyboard.modifiers, ev->key.code, ev->key.state))
      return;

   wlc_keyboard_key(&seat->keyboard, ev->time, ev->key.code, ev->key.state);
}

static void
input_event(struct wl_listener *listener, void *data)
{
   struct wlc_seat *seat;
   struct wlc_compositor *compositor;
   except((seat = wl_container_of(listener, seat, listener.input)) && (compositor = wl_container_of(seat, compositor, seat)));

   struct wlc_output *output = convert_from_wlc_handle(compositor->active.output, "output");

   // Wake up output
   if (output) {
      bool was_asleep = output->state.sleeping;
      wlc_output_set_sleep_ptr(output, false);

      // Skip input event
      if (was_asleep)
         return;
   }

   struct wlc_input_event *ev = data;
   switch (ev->type) {
      case WLC_INPUT_EVENT_MOTION:
         {
            struct wlc_size resolution = (output ? output->resolution : wlc_size_zero);

            struct wlc_pointer_origin pos = {
               chck_clamp(seat->pointer.pos.x + ev->motion.dx, 0, resolution.w),
               chck_clamp(seat->pointer.pos.y + ev->motion.dy, 0, resolution.h),
            };

            if (WLC_INTERFACE_EMIT_EXCEPT(pointer.motion, false, seat->pointer.focused.view, ev->time, &(struct wlc_origin){ pos.x, pos.y }))
               return;

            wlc_pointer_motion(&seat->pointer, ev->time, &pos);
         }
         break;

      case WLC_INPUT_EVENT_MOTION_ABSOLUTE:
         {
            struct wlc_size resolution = (output ? output->resolution : wlc_size_zero);

            struct wlc_pointer_origin pos = {
               ev->motion_abs.x(ev->motion_abs.internal, resolution.w),
               ev->motion_abs.y(ev->motion_abs.internal, resolution.h)
            };

            if (WLC_INTERFACE_EMIT_EXCEPT(pointer.motion, false, seat->pointer.focused.view, ev->time, &(struct wlc_origin){ pos.x, pos.y }))
               return;

            wlc_pointer_motion(&seat->pointer, ev->time, &pos);
         }
         break;

      case WLC_INPUT_EVENT_SCROLL:
         if (WLC_INTERFACE_EMIT_EXCEPT(pointer.scroll, false, seat->pointer.focused.view, ev->time, &seat->keyboard.modifiers, ev->scroll.axis_bits, ev->scroll.amount))
            return;

         wlc_pointer_scroll(&seat->pointer, ev->time, ev->scroll.axis_bits, ev->scroll.amount);
         break;

      case WLC_INPUT_EVENT_BUTTON:
         if (WLC_INTERFACE_EMIT_EXCEPT(pointer.button, false, seat->pointer.focused.view, ev->time, &seat->keyboard.modifiers,  ev->button.code, (enum wlc_button_state)ev->button.state))
            return;

         wlc_pointer_button(&seat->pointer, ev->time, ev->button.code, ev->button.state);
         break;

      case WLC_INPUT_EVENT_KEY:
         seat_handle_key(seat, ev);
         break;

      case WLC_INPUT_EVENT_TOUCH:
         {
            struct wlc_size resolution = (output ? output->resolution : wlc_size_zero);

            struct wlc_origin pos = {
               ev->touch.x(ev->touch.internal, resolution.w),
               ev->touch.y(ev->touch.internal, resolution.h)
            };

            if (WLC_INTERFACE_EMIT_EXCEPT(touch.touch, false, seat->pointer.focused.view, ev->time, &seat->keyboard.modifiers,  ev->touch.type, ev->touch.slot, &pos))
               return;

            if (ev->touch.type == WLC_TOUCH_MOTION || ev->touch.type == WLC_TOUCH_DOWN)
               wlc_pointer_motion(&seat->pointer, ev->time, &(struct wlc_pointer_origin){ pos.x, pos.y });

            wlc_touch_touch(&seat->touch, ev->time, ev->touch.type, ev->touch.slot, &pos);
         }
         break;
   }
}

static void
focus_event(struct wl_listener *listener, void *data)
{
   struct wlc_seat *seat;
   except((seat = wl_container_of(listener, seat, listener.focus)));

   struct wlc_focus_event *ev = data;
   switch (ev->type) {
      case WLC_FOCUS_EVENT_VIEW:
         wlc_keyboard_focus(&seat->keyboard, ev->view);
         wlc_data_device_manager_offer(&seat->manager, wlc_view_get_client(ev->view));
      break;

      default:break;
   }
}

   static void
surface_event(struct wl_listener *listener, void *data)
{
   struct wlc_seat *seat;
   except((seat = wl_container_of(listener, seat, listener.surface)));

   struct wlc_surface_event *ev = data;
   switch (ev->type) {
      case WLC_SURFACE_EVENT_DESTROYED:
         if (ev->surface->view == seat->keyboard.focused.view)
            wlc_keyboard_focus(&seat->keyboard, NULL);
         if (ev->surface->view == seat->pointer.focused.view)
            wlc_pointer_focus(&seat->pointer, NULL, NULL);
      break;

      default:break;
   }
}

void
wlc_seat_release(struct wlc_seat *seat)
{
   if (!seat)
      return;

   wl_list_remove(&seat->listener.input.link);
   wl_list_remove(&seat->listener.focus.link);
   wl_list_remove(&seat->listener.surface.link);

   if (seat->wl.seat)
      wl_global_destroy(seat->wl.seat);

   wlc_data_device_manager_release(&seat->manager);

   wlc_keyboard_release(&seat->keyboard);
   wlc_keymap_release(&seat->keymap);
   wlc_pointer_release(&seat->pointer);
   wlc_touch_release(&seat->touch);

   memset(seat, 0, sizeof(struct wlc_seat));
}

bool
wlc_seat(struct wlc_seat *seat)
{
   assert(seat);
   memset(seat, 0, sizeof(struct wlc_seat));

   if (!wlc_data_device_manager(&seat->manager))
      goto fail;

   seat->listener.input.notify = input_event;
   seat->listener.focus.notify = focus_event;
   seat->listener.surface.notify = surface_event;
   wl_signal_add(&wlc_system_signals()->input, &seat->listener.input);
   wl_signal_add(&wlc_system_signals()->focus, &seat->listener.focus);
   wl_signal_add(&wlc_system_signals()->surface, &seat->listener.surface);

   /* we need to do this since libxkbcommon uses secure_getenv,
    * and we advice compositors to sgid to input group for now. */
   struct xkb_rule_names rules;
   memset(&rules, 0, sizeof(rules));
   rules.rules = getenv("XKB_DEFAULT_RULES");
   rules.model = getenv("XKB_DEFAULT_MODEL");
   rules.layout = getenv("XKB_DEFAULT_LAYOUT");
   rules.variant = getenv("XKB_DEFAULT_VARIANT");
   rules.options = getenv("XKB_DEFAULT_OPTIONS");

   if (!wlc_keymap(&seat->keymap, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS) ||
       !wlc_keyboard(&seat->keyboard, &seat->keymap) ||
       !wlc_pointer(&seat->pointer) ||
       !wlc_touch(&seat->touch))
      goto fail;

   if (!(seat->wl.seat = wl_global_create(wlc_display(), &wl_seat_interface, 4, seat, wl_seat_bind)))
      goto shell_interface_fail;

   return seat;

shell_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind shell interface");
fail:
   wlc_seat_release(seat);
   return false;
}
