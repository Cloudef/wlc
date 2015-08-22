#ifndef _WLC_INTERNAL_H_
#define _WLC_INTERNAL_H_

#include <wlc/wlc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server.h>
#include "resources/resources.h"

#if __GNUC__
#  define WLC_LOG_ATTR(x, y) __attribute__((format(printf, x, y)))
#else
#  define WLC_LOG_ATTR(x, y)
#endif

struct wlc_activate_event {
   int vt; // if != 0, means vt switch
   bool active;
};

enum wlc_focus_event_type {
   WLC_FOCUS_EVENT_VIEW,
   WLC_FOCUS_EVENT_OUTPUT
};

struct wlc_focus_event {
   union {
      struct wlc_output *output;
      struct wlc_view *view;
   };

   enum wlc_focus_event_type type;
};

enum wlc_surface_event_type {
   WLC_SURFACE_EVENT_CREATED,
   WLC_SURFACE_EVENT_DESTROYED,
   WLC_SURFACE_EVENT_REQUEST_VIEW_ATTACH,
   WLC_SURFACE_EVENT_REQUEST_VIEW_POPUP,
};

struct wlc_surface_event {
   union {
      // WLC_INPUT_EVENT_CREATED (no data)
      // WLC_INPUT_EVENT_DESTROYED (no data)

      // WLC_SURFACE_EVENT_REQUEST_VIEW_ATTACH
      struct wlc_surface_event_request_view_attach {
         enum wlc_shell_surface_type {
            WLC_SHELL_SURFACE,
            WLC_XDG_SURFACE,
            WLC_SHELL_SURFACE_TYPE_LAST
         } type;
         wlc_resource shell_surface;
      } attach;

      // WLC_SURFACE_EVENT_REQUEST_VIEW_POPUP
      struct wlc_surface_event_request_view_popup {
         struct wlc_surface *parent;
         struct wlc_origin origin;
         wlc_resource resource;
      } popup;
   };

   struct wlc_surface *surface;
   enum wlc_surface_event_type type;
};

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
         WLC_NONULL double (*x)(void *internal, uint32_t width);
         WLC_NONULL double (*y)(void *internal, uint32_t height);
         void *internal; // Pass to x / y functions
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
         WLC_NONULL double (*x)(void *internal, uint32_t width);
         WLC_NONULL double (*y)(void *internal, uint32_t height);
         void *internal; // Pass to x / y functions
         int32_t slot;
         enum wlc_touch_type type;
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
   WLC_OUTPUT_EVENT_SURFACE,
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
      // No data, compositor just tells backend to update outputs.

      // WLC_OUTPUT_EVENT_SURFACE
      // Used for TTY switching mainly, outputs send this even whenever their backend surface is set.
      struct wlc_output_event_surface {
         struct wlc_output *output;
      } surface;
   };

   enum wlc_output_event_type type;
};

enum wlc_render_event_type {
   WLC_RENDER_EVENT_POINTER,
};

struct wlc_render_event {
   struct wlc_output *output;
   enum wlc_render_event_type type;
};

struct wlc_system_signals {
   struct wl_signal terminate; // data: null (wlc.c)
   struct wl_signal activate;  // data: struct wlc_activate_event (wlc/wlc.c, compositor/seat/seat.c)
   struct wl_signal compositor;// data: null (compositor.c)
   struct wl_signal focus;     // data: struct wlc_focus_event (view/view.c, output/output.c)
   struct wl_signal surface;   // data: struct wlc_surface_event (compositor/compositor.c, shell/shell.c, shell/xdg-shell.c, resources/types/surface.c)
   struct wl_signal input;     // data: struct wlc_input_event (session/udev.c, backend/x11.c)
   struct wl_signal output;    // data: struct wlc_output_event (backend/x11.c, backend/drm.c, session/udev.c)
   struct wl_signal render;    // data: struct wlc_render (compositor/output.c)
   struct wl_signal xwayland;  // data: bool <false/true> (xwayland/xwayland.c)
};

/** Pointer to the system signals */
struct wlc_system_signals* wlc_system_signals(void);

/** Types of debug for  wlc_dlog */
enum wlc_debug {
   WLC_DBG_HANDLE,
   WLC_DBG_RENDER,
   WLC_DBG_RENDER_LOOP,
   WLC_DBG_FOCUS,
   WLC_DBG_XWM,
   WLC_DBG_KEYBOARD,
   WLC_DBG_COMMIT,
   WLC_DBG_LAST,
};

/** Log through wlc's log system. */
WLC_NONULL WLC_LOG_ATTR(2, 3) void wlc_log(enum wlc_log_type type, const char *fmt, ...);

/** va_list version of wlc_log. */
WLC_NONULL void wlc_vlog(enum wlc_log_type type, const char *fmt, va_list ap);

/** Debug log, the output is controlled by WLC_DEBUG env variable. */
WLC_NONULL WLC_LOG_ATTR(2, 3) void wlc_dlog(enum wlc_debug dbg, const char *fmt, ...);

/** Use only on fatals, currently only wlc.c */
WLC_NONULL WLC_LOG_ATTR(1, 2) static inline void
die(const char *format, ...)
{
   va_list vargs;
   va_start(vargs, format);
   wlc_vlog(WLC_LOG_ERROR, format, vargs);
   va_end(vargs);
   exit(EXIT_FAILURE);
}

/** Get current time anywhere. */
uint32_t wlc_get_time(struct timespec *out_ts);

/** Used to indicate whether TTY is activate, but effectively makes wlc compositor sleep. */
void wlc_set_active(bool active);
bool wlc_get_active(void);

/** Pointer to the event loop. */
struct wl_event_loop* wlc_event_loop();

/** Pointer to the wayland display. */
struct wl_display* wlc_display();

/** Emit interface callback with variable arguments. */
#define WLC_INTERFACE_EMIT(x, ...) { if (wlc_interface()->x) wlc_interface()->x(__VA_ARGS__); }

/** Emit interface callback with variable arguments inside condition expecting a return value of b. */
#define WLC_INTERFACE_EMIT_EXCEPT(x, b, ...) (wlc_interface()->x && wlc_interface()->x(__VA_ARGS__) == b)

/**
 * Pointer to the current interface implementation.
 * Most likely you want to use the WLC_INTERFACE macros however.
 */
const struct wlc_interface* wlc_interface(void);

/** This is here so fd.c sees it, do not use. */
void wlc_cleanup(void);

#endif /* _WLC_INTERNAL_H_ */
