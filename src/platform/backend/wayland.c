#include <wayland-server.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-util.h>
#include <chck/math/math.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "internal.h"
#include "macros.h"
#include "wayland.h"
#include "backend.h"
#include "compositor/compositor.h"
#include "compositor/output.h"
#include "compositor/seat/keyboard.h"
#include "compositor/seat/keymap.h"

struct wayland_surface {
   struct wlc_compositor *compositor;
   struct wl_surface *surface;
   struct wl_shell_surface *shell_surface;
   struct wl_egl_window *egl_window;
};

static struct wayland_backend {
   struct wlc_backend *backend;

   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_compositor *compositor;
   struct wl_shell *shell;
   struct wl_seat *seat;
   struct wl_keyboard *keyboard;
   struct wl_pointer *pointer;
   struct wl_touch *touch;
   struct wl_event_source *event_source;
} wayland;

struct xy_event {
   double x, y;
};

static double
xy_event_get_x(void *internal, uint32_t width)
{
   struct xy_event *event = internal;
   return chck_clamp(event->x, 0, width);
}

static double
xy_event_get_y(void *internal, uint32_t height)
{
   struct xy_event *event = internal;
   return chck_clamp(event->y, 0, height);
}

static struct wlc_output *
output_for_wl_surface(struct chck_pool *outputs, struct wl_surface *surface)
{
   struct wlc_output *o;
   chck_pool_for_each(outputs, o) {
      struct wayland_surface *wsurface = o->bsurface.internal;
      if (wsurface->surface == surface)
         return o;
   }
   return NULL;
}

static struct wlc_keyboard*
get_keyboard(void)
{
   struct wlc_compositor *compositor;
   except((compositor = wl_container_of(wayland.backend, compositor, backend)));
   return &compositor->seat.keyboard;
}

static int
wayland_event(int fd, uint32_t mask, void *data)
{
   (void)fd;

   struct wlc_compositor *compositor;
   int count = 0;

   except((compositor = wl_container_of(data, compositor, backend)));

   if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
      wlc_terminate();
      return 0;
   }

   if (mask & WL_EVENT_READABLE)
      count = wl_display_dispatch(wayland.display);
   if (mask & WL_EVENT_WRITABLE)
      wl_display_flush(wayland.display);

   if (mask == 0) {
      count = wl_display_dispatch_pending(wayland.display);
      wl_display_flush(wayland.display);
   }

   return count;
}

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
           uint32_t serial, struct wl_surface *surface,
           wl_fixed_t surface_x, wl_fixed_t surface_y)
{
   struct wayland_surface *wsurface = data;
   struct wlc_compositor *compositor = wsurface->compositor;

   struct wlc_output *output = output_for_wl_surface(&compositor->outputs.pool, surface);
   if (!output)
      return;

   struct wlc_output_event oev = { .active = { output }, .type = WLC_OUTPUT_EVENT_ACTIVE };
   wl_signal_emit(&wlc_system_signals()->output, &oev);

   wl_pointer_set_cursor(pointer, 0, NULL, 0, 0);
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
           uint32_t serial, struct wl_surface *surface)
{
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
            uint32_t time, wl_fixed_t fixed_x, wl_fixed_t fixed_y)
{
   struct wlc_input_event ev = {0};
   struct xy_event motion = {
      .x = wl_fixed_to_double(fixed_x),
      .y = wl_fixed_to_double(fixed_y)
   };
   ev.type = WLC_INPUT_EVENT_MOTION_ABSOLUTE;
   ev.time = time;
   ev.motion_abs.x = xy_event_get_x;
   ev.motion_abs.y = xy_event_get_y;
   ev.motion_abs.internal = &motion;
   wl_signal_emit(&wlc_system_signals()->input, &ev);
}

static void
pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
            uint32_t time, uint32_t button, uint32_t state)
{
   struct wlc_input_event ev = {0};
   ev.type = WLC_INPUT_EVENT_BUTTON;
   ev.time = time;
   ev.button.code = button;
   ev.button.state = state;
   wl_signal_emit(&wlc_system_signals()->input, &ev);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *pointer,
          uint32_t time, uint32_t axis, wl_fixed_t value)
{
   struct wlc_input_event ev = {0};
   ev.type = WLC_INPUT_EVENT_SCROLL;
   ev.time = time;
   if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
      ev.scroll.amount[0] = wl_fixed_to_double(value);
      ev.scroll.axis_bits = WLC_SCROLL_AXIS_VERTICAL;
   } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
      ev.scroll.amount[1] = wl_fixed_to_double(value);
      ev.scroll.axis_bits = WLC_SCROLL_AXIS_HORIZONTAL;
   }
   wl_signal_emit(&wlc_system_signals()->input, &ev);
}

static const struct wl_pointer_listener pointer_listener = {
   .enter = pointer_handle_enter,
   .leave = pointer_handle_leave,
   .motion = pointer_handle_motion,
   .button = pointer_handle_button,
   .axis = pointer_handle_axis,
};

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
             uint32_t format, int fd, uint32_t size)
{
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
            uint32_t serial, struct wl_surface *surface,
            struct wl_array *keys)
{
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
            uint32_t serial, struct wl_surface *surface)
{
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
          uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
   struct wlc_input_event ev = {0};
   ev.type = WLC_INPUT_EVENT_KEY;
   ev.time = time;
   ev.key.code = key;
   ev.key.state = state;
   wl_signal_emit(&wlc_system_signals()->input, &ev);
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
           uint32_t serial, uint32_t mods_depressed,
           uint32_t mods_latched, uint32_t mods_locked,
           uint32_t group)
{
   struct wlc_keyboard *kbd = get_keyboard();

   if (!kbd->keymap)
      return;

   xkb_state_update_mask(kbd->state.xkb,
                         wlc_keymap_get_mod_mask(kbd->keymap, mods_depressed),
                         wlc_keymap_get_mod_mask(kbd->keymap, mods_latched),
                         wlc_keymap_get_mod_mask(kbd->keymap, mods_locked),
                         0, 0, group);

   wlc_keyboard_update_modifiers(kbd, NULL);
}

static const struct wl_keyboard_listener keyboard_listener = {
   .keymap = keyboard_handle_keymap,
   .enter = keyboard_handle_enter,
   .leave = keyboard_handle_leave,
   .key = keyboard_handle_key,
   .modifiers = keyboard_handle_modifiers,
};

static void
touch_event_xy(uint32_t time, wl_fixed_t fixed_x, wl_fixed_t fixed_y, int32_t id, enum wlc_touch_type type)
{
   struct xy_event touch = {
      .x = wl_fixed_to_double(fixed_x),
      .y = wl_fixed_to_double(fixed_y)
   };

   struct wlc_input_event ev = {0};
   ev.type = WLC_INPUT_EVENT_TOUCH;
   ev.time = time;
   ev.touch.x = xy_event_get_x;
   ev.touch.y = xy_event_get_y;
   ev.touch.internal = &touch;
   ev.touch.slot = id;
   ev.touch.type = type;
   wl_signal_emit(&wlc_system_signals()->input, &ev);
}

static void
touch_handle_down(void *data, struct wl_touch *wl_touch,
        uint32_t serial, uint32_t time, struct wl_surface *surface,
        int32_t id, wl_fixed_t fixed_x, wl_fixed_t fixed_y)
{
   touch_event_xy(time, fixed_x, fixed_y, id, WLC_TOUCH_DOWN);
}

static void
touch_handle_up(void *data, struct wl_touch *wl_touch,
      uint32_t serial, uint32_t time, int32_t id)
{
   struct wlc_input_event ev = {0};
   ev.type = WLC_INPUT_EVENT_TOUCH;
   ev.time = time;
   ev.touch.slot = id;
   ev.touch.type = WLC_TOUCH_UP;
   wl_signal_emit(&wlc_system_signals()->input, &ev);
}

static void
touch_handle_motion(void *data, struct wl_touch *wl_touch,
          uint32_t time, int32_t id, wl_fixed_t fixed_x, wl_fixed_t fixed_y)
{
   touch_event_xy(time, fixed_x, fixed_y, id, WLC_TOUCH_MOTION);
}

static void
touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
   struct wlc_input_event ev = {0};
   ev.type = WLC_INPUT_EVENT_TOUCH;
   ev.touch.type = WLC_TOUCH_FRAME;
   wl_signal_emit(&wlc_system_signals()->input, &ev);
}

static void
touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
   struct wlc_input_event ev = {0};
   ev.type = WLC_INPUT_EVENT_TOUCH;
   ev.touch.type = WLC_TOUCH_CANCEL;
   wl_signal_emit(&wlc_system_signals()->input, &ev);
}

static const struct wl_touch_listener touch_listener = {
   touch_handle_down,
   touch_handle_up,
   touch_handle_motion,
   touch_handle_frame,
   touch_handle_cancel,
};

static void
input_handle_capabilities(void *data, struct wl_seat *seat, enum wl_seat_capability caps)
{
   struct wayland_backend *wayland = data;

   if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wayland->pointer) {
      wayland->pointer = wl_seat_get_pointer(seat);
      wl_pointer_set_user_data(wayland->pointer, wayland);
      wl_pointer_add_listener(wayland->pointer, &pointer_listener, wayland);
   } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && wayland->pointer) {
      wl_pointer_release(wayland->pointer);
      wayland->pointer = NULL;
   }

   if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wayland->keyboard) {
      wayland->keyboard = wl_seat_get_keyboard(seat);
      wl_keyboard_set_user_data(wayland->keyboard, wayland);
      wl_keyboard_add_listener(wayland->keyboard, &keyboard_listener, wayland);
   } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && wayland->keyboard) {
      wl_keyboard_release(wayland->keyboard);
      wayland->keyboard = NULL;
   }

   if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !wayland->touch) {
      wayland->touch = wl_seat_get_touch(seat);
      wl_touch_set_user_data(wayland->touch, wayland);
      wl_touch_add_listener(wayland->touch, &touch_listener, wayland);
   } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && wayland->touch) {
      wl_touch_release(wayland->touch);
      wayland->touch = NULL;
   }
}

static void
input_handle_name(void *data, struct wl_seat *seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
   .capabilities = input_handle_capabilities,
   .name = input_handle_name,
};

static void
handle_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial)
{
   wl_shell_surface_pong(shell_surface, serial);
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface, uint32_t edges, int32_t width, int32_t height)
{
}

static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
   .ping = handle_ping,
   .configure = handle_configure,
   .popup_done = handle_popup_done
};

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
   struct wayland_backend *wayland = data;

   if (!strcmp(interface, "wl_compositor")) {
      wayland->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
   } else if (!strcmp(interface, "wl_shell")) {
      wayland->shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
   } else if (!strcmp(interface, "wl_seat")) {
      wayland->seat = wl_registry_bind(registry, name, &wl_seat_interface, 3);
      wl_seat_add_listener(wayland->seat, &seat_listener, wayland);
      wl_seat_set_user_data(wayland->seat, wayland);
   }
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
   .global = registry_handle_global,
   .global_remove = registry_handle_global_remove
};

static bool
page_flip(struct wlc_backend_surface *bsurface)
{
   struct timespec ts;
   wlc_get_time(&ts);
   struct wlc_output *o;
   wlc_output_finish_frame(wl_container_of(bsurface, o, bsurface), &ts);
   return true;
}

static void
surface_release(struct wlc_backend_surface *bsurface)
{
   struct wayland_surface *surface = bsurface->internal;
   if (!surface)
      return;

   if (surface->egl_window)
      wl_egl_window_destroy(surface->egl_window);

   if (surface->shell_surface)
      wl_shell_surface_destroy(surface->shell_surface);

   if (surface->surface)
      wl_surface_destroy(surface->surface);
}

static void
fake_information(struct wlc_output_information *info, uint32_t id)
{
   assert(info);
   wlc_output_information(info);
   chck_string_set_cstr(&info->make, "Wayland", false);
   chck_string_set_cstr(&info->model, "Wayland Window", false);
   info->connector = WLC_CONNECTOR_WLC;
   info->connector_id = id;

   struct wlc_output_mode mode = {0};
   mode.refresh = 60 * 1000; // mHz
   mode.width = 800;
   mode.height = 480;
   mode.flags = WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
   wlc_output_information_add_mode(info, &mode);
}

static bool
add_output(struct wayland_surface *surface, struct wlc_output_information *info)
{
   struct wlc_backend_surface bsurface;
   if (!wlc_backend_surface(&bsurface, surface_release, sizeof(struct wayland_surface)))
      return false;

   struct wayland_surface *wsurface = bsurface.internal;
   memcpy(wsurface, surface, sizeof(*wsurface));

   bsurface.window = surface->egl_window;
   bsurface.display = wayland.display;
   bsurface.display_type = EGL_PLATFORM_WAYLAND_EXT;
   bsurface.api.page_flip = page_flip;

   struct wlc_output_event ev = { .add = { &bsurface, info }, .type = WLC_OUTPUT_EVENT_ADD };
   wl_signal_emit(&wlc_system_signals()->output, &ev);
   return true;
}

static uint32_t
update_outputs(struct chck_pool *outputs)
{
   const char *env;
   uint32_t alive = 0;
   uint32_t fakes = 1;
   uint32_t count = 0;

   if (outputs) {
      struct wlc_output *o;
      chck_pool_for_each(outputs, o) {
         if (o->bsurface.window)
            ++alive;
      }
   }

   if ((env = getenv("WLC_OUTPUTS"))) {
      chck_cstr_to_u32(env, &fakes);
      fakes = chck_maxu32(fakes, 1);
   }

   if (alive >= fakes)
      return 0;

   for (uint32_t i = 0; i < fakes; ++i) {
      struct wayland_surface surface = {0};

      surface.surface = wl_compositor_create_surface(wayland.compositor);
      surface.shell_surface = wl_shell_get_shell_surface(wayland.shell, surface.surface);

      wl_shell_surface_add_listener(surface.shell_surface, &shell_surface_listener, &surface);
      wl_shell_surface_set_title(surface.shell_surface, "sway-wayland");
      wl_shell_surface_set_toplevel(surface.shell_surface);

      surface.egl_window = wl_egl_window_create(surface.surface, 800, 480);
      surface.compositor = wayland.compositor;

      struct wlc_output_information info;
      fake_information(&info, i + 1);
      count += (add_output(&surface, &info) ? 1 : 0);
   }

   return count;
}

static void
terminate(void)
{
   if (wayland.shell)
      wl_shell_destroy(wayland.shell);

   if (wayland.seat)
      wl_seat_destroy(wayland.seat);

   if (wayland.compositor)
      wl_compositor_destroy(wayland.compositor);

   if (wayland.registry)
      wl_registry_destroy(wayland.registry);

   if (wayland.display) {
      wl_display_flush(wayland.display);
      wl_display_disconnect(wayland.display);
   }

   if (wayland.event_source)
      wl_event_source_remove(wayland.event_source);

   memset(&wayland, 0, sizeof(wayland));
}

bool
wlc_wayland(struct wlc_backend *backend)
{
   wayland.backend = backend;

   if (!(wayland.display = wl_display_connect("wayland-0")))
      goto display_open_fail;

   if (!(wayland.registry = wl_display_get_registry(wayland.display)))
      goto get_registry_fail;

   wl_registry_add_listener(wayland.registry, &registry_listener, &wayland);
   wl_display_roundtrip(wayland.display);

   if (!(wayland.event_source = wl_event_loop_add_fd(wlc_event_loop(), wl_display_get_fd(wayland.display), WL_EVENT_READABLE, wayland_event, backend)))
      goto event_source_fail;

   wl_event_source_check(wayland.event_source);

   backend->api.update_outputs = update_outputs;
   backend->api.terminate = terminate;
   return true;

display_open_fail:
   wlc_log(WLC_LOG_WARN, "Failed to open Wayland display");
   goto fail;
get_registry_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get Wayland display registry");
   goto fail;
event_source_fail:
   wlc_log(WLC_LOG_WARN, "Failed to add Wayland event source");
   goto fail;
fail:
   terminate();
   return false;
}
