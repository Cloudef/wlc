#ifndef _WLC_H_
#define _WLC_H_

#include <wlc/geometry.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdarg.h>

#if __GNUC__
#  define WLC_LOG_ATTR(x, y) __attribute__((format(printf, x, y)))
#else
#  define WLC_LOG_ATTR(x, y)
#endif

typedef uintptr_t wlc_handle;

/** wlc_log(), wlc_vlog(); */
enum wlc_log_type {
   WLC_LOG_INFO,
   WLC_LOG_WARN,
   WLC_LOG_ERROR,
};

/** wlc_view_get_state(); */
enum wlc_view_state_bit {
   WLC_BIT_MAXIMIZED = 1<<0,
   WLC_BIT_FULLSCREEN = 1<<1,
   WLC_BIT_RESIZING = 1<<2,
   WLC_BIT_MOVING = 1<<3,
   WLC_BIT_ACTIVATED = 1<<4,
};

/** wlc_view_get_type(); */
enum wlc_view_type_bit {
   WLC_BIT_OVERRIDE_REDIRECT = 1<<0, // Override redirect (x11)
   WLC_BIT_UNMANAGED = 1<<1, // Tooltips, DnD's, menus (x11)
   WLC_BIT_SPLASH = 1<<2, // Splash screens (x11)
   WLC_BIT_MODAL = 1<<3, // Modal windows (x11)
   WLC_BIT_POPUP = 1<<4, // xdg-shell, wl-shell popups
};

/** Mods in interface.keyboard.key function. */
enum wlc_modifier_bit {
   WLC_BIT_MOD_SHIFT = 1<<0,
   WLC_BIT_MOD_CAPS = 1<<1,
   WLC_BIT_MOD_CTRL = 1<<2,
   WLC_BIT_MOD_ALT = 1<<3,
   WLC_BIT_MOD_MOD2 = 1<<4,
   WLC_BIT_MOD_MOD3 = 1<<5,
   WLC_BIT_MOD_LOGO = 1<<6,
   WLC_BIT_MOD_MOD5 = 1<<7,
};

/** Leds in interface.keyboard.key function. */
enum wlc_led_bit {
   WLC_BIT_LED_NUM = 1<<0,
   WLC_BIT_LED_CAPS = 1<<1,
   WLC_BIT_LED_SCROLL = 1<<2,
};

/** State in interface.keyboard.key function. */
enum wlc_key_state {
   WLC_KEY_STATE_RELEASED = 0,
   WLC_KEY_STATE_PRESSED = 1
};

/** State in interface.pointer.button function. */
enum wlc_button_state {
   WLC_BUTTON_STATE_RELEASED = 0,
   WLC_BUTTON_STATE_PRESSED = 1
};

/** Axis in interface.pointer.scroll function. */
enum wlc_scroll_axis_bit {
   WLC_SCROLL_AXIS_VERTICAL = 1<<0,
   WLC_SCROLL_AXIS_HORIZONTAL = 1<<1
};

/** Type in interface.touch.touch function */
enum wlc_touch_type {
   WLC_TOUCH_DOWN,
   WLC_TOUCH_UP,
   WLC_TOUCH_MOTION,
   WLC_TOUCH_FRAME,
   WLC_TOUCH_CANCEL
};

/** State of keyboard modifiers in various functions. */
struct wlc_modifiers {
   uint32_t leds, mods;
};

/** Interface struct for communicating with wlc. */
struct wlc_interface {
   struct {
      /** Output was created. Return false if you want to destroy the output. (ex. failed to allocate data related to view) */
      bool (*created)(wlc_handle output);

      /** Output was destroyed. */
      void (*destroyed)(wlc_handle output);

      /** Output got or lost focus. */
      void (*focus)(wlc_handle output, bool focus);

      /** Output resolution changed. */
      void (*resolution)(wlc_handle output, const struct wlc_size *from, const struct wlc_size *to);
   } output;

   struct {
      /** View was created. Return false if you want to destroy the view. (ex. failed to allocate data related to view) */
      bool (*created)(wlc_handle view);

      /** View was destroyed. */
      void (*destroyed)(wlc_handle view);

      /** View got or lost focus. */
      void (*focus)(wlc_handle view, bool focus);

      /** View was moved to output. */
      void (*move_to_output)(wlc_handle view, wlc_handle from_output, wlc_handle to_output);

      struct {
         /** Request to set given geometry for view. Apply using wlc_view_set_geometry to agree. */
         void (*geometry)(wlc_handle view, const struct wlc_geometry*);

         /** Request to disable or enable the given state for view. Apply using wlc_view_set_state to agree. */
         void (*state)(wlc_handle view, enum wlc_view_state_bit, bool toggle);
      } request;
   } view;

   struct {
      /** Key event was triggered, view handle will be zero if there was no focus. */
      bool (*key)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint32_t key, uint32_t sym, enum wlc_key_state);
   } keyboard;

   struct {
      /** Button event was triggered, view handle will be zero if there was no focus. */
      bool (*button)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint32_t button, enum wlc_button_state);

      /** Scroll event was triggered, view handle will be zero if there was no focus. */
      bool (*scroll)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint8_t axis_bits, double amount[2]);

      /** Motion event was triggered, view handle will be zero if there was no focus. */
      bool (*motion)(wlc_handle view, uint32_t time, const struct wlc_origin*);
   } pointer;

   struct {
      /** Touch event was triggered, view handle will be zero if there was no focus. */
      bool (*touch)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, enum wlc_touch_type, int32_t slot, const struct wlc_origin*);
   } touch;
};

/** -- Core API */

/** Initialize wlc. Returns false on failure. */
bool wlc_init(const struct wlc_interface *interface, int argc, char *argv[]);

/** Terminate wlc. */
void wlc_terminate(void);

/** Run event loop. */
void wlc_run(void);

/** Get current log file. */
FILE* wlc_get_log_file(void);

/** Set log file. */
void wlc_set_log_file(FILE *out);

/** Log through wlc's log system. */
WLC_LOG_ATTR(2, 3) void wlc_log(enum wlc_log_type type, const char *fmt, ...);

/** va_list version of wlc_log. */
void wlc_vlog(enum wlc_log_type type, const char *fmt, va_list ap);

/** Link custom data to handle. */
void wlc_handle_set_user_data(wlc_handle handle, const void *userdata);

/** Get linked custom data from handle. */
void* wlc_handle_get_user_data(wlc_handle handle);

/** -- Output API */

/** Get outputs. Returned array is a direct reference, careful when moving and destroying handles. */
const wlc_handle* wlc_get_outputs(size_t *out_memb);

/** Get focused output. */
wlc_handle wlc_get_focused_output(void);

/** Get sleep state. */
bool wlc_output_get_sleep(wlc_handle output);

/** Wake up / sleep. */
void wlc_output_set_sleep(wlc_handle output, bool sleep);

/** Get resolution. */
const struct wlc_size* wlc_output_get_resolution(wlc_handle output);

/** Set resolution. */
void wlc_output_set_resolution(wlc_handle output, const struct wlc_size *resolution);

/** Get current visibility bitmask. */
uint32_t wlc_output_get_mask(wlc_handle output);

/** Set visibility bitmask. */
void wlc_output_set_mask(wlc_handle output, uint32_t mask);

/** Get pixels. If you return true in callback, the rgba data will be not freed. Do this if you don't want to copy the buffer. */
void wlc_output_get_pixels(wlc_handle output, bool (*pixels)(const struct wlc_size *size, uint8_t *rgba, void *arg), void *arg);

/** Get views in stack order. Returned array is a direct reference, careful when moving and destroying handles. */
const wlc_handle* wlc_output_get_views(wlc_handle output, size_t *out_memb);

/**
 * Get mutable views in creation order. Returned array is a direct reference, careful when moving and destroying handles.
 * This is mainly useful for wm's who need another view stack for inplace sorting.
 * For example tiling wms, may want to use this to keep their tiling order separated from floating order.
 */
wlc_handle* wlc_output_get_mutable_views(wlc_handle output, size_t *out_memb);

/** Set views in stack order. This will also change mutable views. Returns false on failure. */
bool wlc_output_set_views(wlc_handle output, const wlc_handle *views, size_t memb);

/** Focus output. Pass zero for no focus. */
void wlc_output_focus(wlc_handle output);

/** -- View API */

/** Focus view. Pass zero for no focus. */
void wlc_view_focus(wlc_handle view);

/** Close view. */
void wlc_view_close(wlc_handle view);

/** Get current output. */
wlc_handle wlc_view_get_output(wlc_handle view);

/** Set output. Alternatively you can wlc_output_set_views. */
void wlc_view_set_output(wlc_handle view, wlc_handle output);

/** Send behind everything. */
void wlc_view_send_to_back(wlc_handle view);

/** Send below another view. */
void wlc_view_send_below(wlc_handle view, wlc_handle other);

/** Send above another view. */
void wlc_view_bring_above(wlc_handle view, wlc_handle other);

/** Bring to front of everything. */
void wlc_view_bring_to_front(wlc_handle view);

/** Get current visibility bitmask. */
uint32_t wlc_view_get_mask(wlc_handle view);

/** Set visibility bitmask. */
void wlc_view_set_mask(wlc_handle view, uint32_t mask);

/** Get current geometry. */
const struct wlc_geometry* wlc_view_get_geometry(wlc_handle view);

/** Set geometry. */
void wlc_view_set_geometry(wlc_handle view, const struct wlc_geometry *geometry);

/** Get type bitfield. */
uint32_t wlc_view_get_type(wlc_handle view);

/** Set type bit. Toggle indicates whether it is set or not. */
void wlc_view_set_type(wlc_handle view, enum wlc_view_type_bit type, bool toggle);

/** Get current state bitfield. */
uint32_t wlc_view_get_state(wlc_handle view);

/** Set state bit. Toggle indicates whether it is set or not. */
void wlc_view_set_state(wlc_handle view, enum wlc_view_state_bit state, bool toggle);

/** Get parent view. */
wlc_handle wlc_view_get_parent(wlc_handle view);

/** Set parent view. */
void wlc_view_set_parent(wlc_handle view, wlc_handle parent);

/** Get title. */
const char* wlc_view_get_title(wlc_handle view);

/** Set title. Returns false on failure. */
bool wlc_view_set_title(wlc_handle view, const char *title);

/** Get class. (shell-surface only) */
const char* wlc_view_get_class(wlc_handle view);

/** Set class. Returns false on failure. (shell-surface only) */
bool wlc_view_set_class(wlc_handle view, const char *class_);

/** Get app id. (xdg-surface only) */
const char* wlc_view_get_app_id(wlc_handle view);

/** Set app id. Returns false on failure. (xdg-surface only) */
bool wlc_view_set_app_id(wlc_handle view, const char *app_id);

#endif /* _WLC_H_ */
