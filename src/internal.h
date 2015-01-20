#ifndef _WLC_INTERNAL_H_
#define _WLC_INTERNAL_H_

#include "wlc.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include <wayland-server.h>

#define WLC_INTERFACE_EMIT(x, ...) { if (wlc_interface()->x) wlc_interface()->x(__VA_ARGS__); }
#define WLC_INTERFACE_EMIT_EXCEPT(x, b, ...) (wlc_interface()->x && wlc_interface()->x(__VA_ARGS__) == b)

enum wlc_input_event_type {
   WLC_INPUT_EVENT_MOTION,
   WLC_INPUT_EVENT_MOTION_ABSOLUTE,
   WLC_INPUT_EVENT_BUTTON,
   WLC_INPUT_EVENT_SCROLL,
   WLC_INPUT_EVENT_KEY,
   WLC_INPUT_EVENT_TOUCH,
};

struct wlc_input_event {
   union {
      // WLC_INPUT_EVENT_MOTION (relative)
      struct wlc_input_event_motion {
         double dx, dy;
      } motion;

      // WLC_INPUT_EVENT_MOTION_ABSOLUTE
      struct wlc_input_event_motion_absolute {
         double (*x)(void *internal, uint32_t width);
         double (*y)(void *internal, uint32_t height);
         void *internal;
      } motion_abs;

      // WLC_INPUT_EVENT_BUTTON
      struct wlc_input_event_button {
         uint32_t code;
         enum wl_pointer_button_state state;
      } button;

      // WLC_INPUT_EVENT_SCROLL
      struct wlc_input_event_scroll {
         double amount[2]; // 0 == vertical, 1 == horizontal
         uint8_t axis_bits;
      } scroll;

      // WLC_INPUT_EVENT_KEY
      struct wlc_input_event_key {
         uint32_t code;
         enum wl_keyboard_key_state state;
      } key;

      // WLC_INPUT_EVENT_TOUCH
      struct wlc_input_event_touch {
         enum wlc_touch_type {
            WLC_TOUCH_DOWN,
            WLC_TOUCH_UP,
            WLC_TOUCH_MOTION,
            WLC_TOUCH_FRAME,
            WLC_TOUCH_CANCEL
         } type;
         double (*x)(void *internal, uint32_t width);
         double (*y)(void *internal, uint32_t height);
         void *internal;
         int32_t slot;
      } touch;
   };

   uint32_t time;
   enum wlc_input_event_type type;
};

enum wlc_output_event_type {
   WLC_OUTPUT_EVENT_ADD,
   WLC_OUTPUT_EVENT_REMOVE,
   WLC_OUTPUT_EVENT_ACTIVE,
   WLC_OUTPUT_EVENT_UPDATE,
};

struct wlc_output_event {
   union {
      // WLC_OUTPUT_EVENT_ADD
      struct wlc_output_event_add {
         struct wlc_backend_surface *bsurface;
         struct wlc_output_information *info;
      } add;

      // WLC_OUTPUT_EVENT_REMOVE
      struct wlc_output_evnet_remove {
         struct wlc_output *output;
      } remove;

      // WLC_OUTPUT_EVENT_ACTIVE
      struct wlc_output_event_active {
         struct wlc_output *output;
      } active;

      // WLC_OUTPUT_EVENT_UPDATE
      // no data, compositor just tells backend to update outputs
   };
   enum wlc_output_event_type type;
};

struct wlc_system_signals {
   struct wl_signal terminated;// data: none
   struct wl_signal activated; // data: bool <false/true> (wlc_set_active)
   struct wl_signal surface;   // data: wlc_surface (compositor/compositor.c)
   struct wl_signal input;     // data: struct wlc_input_event (session/udev.c, backend/x11.c)
   struct wl_signal output;    // data: struct wlc_output_event (backend/x11.c, backend/drm.c, session/udev.c)
   struct wl_signal xwayland;  // data: bool <false/true> (xwayland/xwayland.c)
};

enum wlc_debug {
   WLC_DBG_RENDER,
   WLC_DBG_FOCUS,
   WLC_DBG_XWM,
   WLC_DBG_LAST,
};

WLC_LOG_ATTR(1, 2) static inline void
die(const char *format, ...)
{
   va_list vargs;
   va_start(vargs, format);
   wlc_vlog(WLC_LOG_ERROR, format, vargs);
   va_end(vargs);
   exit(EXIT_FAILURE);
}

WLC_LOG_ATTR(2, 3) void wlc_dlog(enum wlc_debug dbg, const char *fmt, ...);

uint32_t wlc_get_time(struct timespec *out_ts);
void wlc_set_active(bool active);
bool wlc_get_active(void);
const struct wlc_interface* wlc_interface(void);
struct wlc_system_signals* wlc_system_signals(void);
struct wl_event_loop* wlc_event_loop();
struct wl_display* wlc_display();
void wlc_cleanup(void);

#endif /* _WLC_INTERNAL_H_ */
