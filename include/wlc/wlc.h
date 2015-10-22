#ifndef _WLC_H_
#define _WLC_H_

#include <wlc/defines.h>
#include <wlc/geometry.h>
#include <xkbcommon/xkbcommon-keysyms.h>

struct wlc_event_source;

struct xkb_state;
struct xkb_keymap;
struct libinput_device;

/** wlc_log(), wlc_vlog(); */
enum wlc_log_type {
   WLC_LOG_INFO,
   WLC_LOG_WARN,
   WLC_LOG_ERROR,
   WLC_LOG_WAYLAND,
};

/** wlc_get_backend_type(); */
enum wlc_backend_type {
   WLC_BACKEND_NONE,
   WLC_BACKEND_DRM,
   WLC_BACKEND_X11,
};

/** mask in wlc_event_loop_add_fd(); */
enum wlc_event_bit {
   WLC_EVENT_READABLE = 0x01,
   WLC_EVENT_WRITABLE = 0x02,
   WLC_EVENT_HANGUP = 0x04,
   WLC_EVENT_ERROR = 0x08,
};

/** wlc_output_get_connector_type(); */
enum wlc_connector_type {
   /* used when running wlc with backend (e.g. x11) that does not use real output. */
   WLC_CONNECTOR_WLC,

   /* these are based on xf86drm.h */
   WLC_CONNECTOR_UNKNOWN,
   WLC_CONNECTOR_VGA,
   WLC_CONNECTOR_DVII,
   WLC_CONNECTOR_DVID,
   WLC_CONNECTOR_DVIA,
   WLC_CONNECTOR_COMPOSITE,
   WLC_CONNECTOR_SVIDEO,
   WLC_CONNECTOR_LVDS,
   WLC_CONNECTOR_COMPONENT,
   WLC_CONNECTOR_DIN,
   WLC_CONNECTOR_DP,
   WLC_CONNECTOR_HDMIA,
   WLC_CONNECTOR_HDMIB,
   WLC_CONNECTOR_TV,
   WLC_CONNECTOR_eDP,
   WLC_CONNECTOR_VIRTUAL,
   WLC_CONNECTOR_DSI,
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

/** wlc_view_set_geometry(); Edges in interface interface.view.request.resize function. */
enum wlc_resize_edge {
   WLC_RESIZE_EDGE_NONE = 0,
   WLC_RESIZE_EDGE_TOP = 1,
   WLC_RESIZE_EDGE_BOTTOM = 2,
   WLC_RESIZE_EDGE_LEFT = 4,
   WLC_RESIZE_EDGE_TOP_LEFT = 5,
   WLC_RESIZE_EDGE_BOTTOM_LEFT = 6,
   WLC_RESIZE_EDGE_RIGHT = 8,
   WLC_RESIZE_EDGE_TOP_RIGHT = 9,
   WLC_RESIZE_EDGE_BOTTOM_RIGHT = 10,
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
   WLC_KEY_STATE_PRESSED = 1,
};

/** State in interface.pointer.button function. */
enum wlc_button_state {
   WLC_BUTTON_STATE_RELEASED = 0,
   WLC_BUTTON_STATE_PRESSED = 1,
};

/** Axis in interface.pointer.scroll function. */
enum wlc_scroll_axis_bit {
   WLC_SCROLL_AXIS_VERTICAL = 1<<0,
   WLC_SCROLL_AXIS_HORIZONTAL = 1<<1,
};

/** Type in interface.touch.touch function */
enum wlc_touch_type {
   WLC_TOUCH_DOWN,
   WLC_TOUCH_UP,
   WLC_TOUCH_MOTION,
   WLC_TOUCH_FRAME,
   WLC_TOUCH_CANCEL,
};

/** State of keyboard modifiers in various functions. */
struct wlc_modifiers {
   uint32_t leds, mods;
};

/** Interface struct for communicating with wlc. */
struct wlc_interface {
   struct {
      /** Output was created. Return false if you want to destroy the output. (e.g. failed to allocate data related to view) */
      bool (*created)(wlc_handle output);

      /** Output was destroyed. */
      void (*destroyed)(wlc_handle output);

      /** Output got or lost focus. */
      void (*focus)(wlc_handle output, bool focus);

      /** Output resolution changed. */
      WLC_NONULL void (*resolution)(wlc_handle output, const struct wlc_size *from, const struct wlc_size *to);
   } output;

   struct {
      /** View was created. Return false if you want to destroy the view. (e.g. failed to allocate data related to view) */
      bool (*created)(wlc_handle view);

      /** View was destroyed. */
      void (*destroyed)(wlc_handle view);

      /** View got or lost focus. */
      void (*focus)(wlc_handle view, bool focus);

      /** View was moved to output. */
      void (*move_to_output)(wlc_handle view, wlc_handle from_output, wlc_handle to_output);

      struct {
         /** Request to set given geometry for view. Apply using wlc_view_set_geometry to agree. */
         WLC_NONULL void (*geometry)(wlc_handle view, const struct wlc_geometry*);

         /** Request to disable or enable the given state for view. Apply using wlc_view_set_state to agree. */
         void (*state)(wlc_handle view, enum wlc_view_state_bit, bool toggle);

         /** Request to move itself. Start a interactive move to agree. */
         WLC_NONULL void (*move)(wlc_handle view, const struct wlc_origin *origin);

         /** Request to resize itself with the given edges. Start a interactive resize to agree. */
         WLC_NONULL void (*resize)(wlc_handle view, uint32_t edges, const struct wlc_origin *origin);
      } request;
   } view;

   struct {
      /** Key event was triggered, view handle will be zero if there was no focus. Return true to prevent sending the event to clients. */
      WLC_NONULL bool (*key)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint32_t key, enum wlc_key_state);
   } keyboard;

   struct {
      /** Button event was triggered, view handle will be zero if there was no focus. Return true to prevent sending the event to clients. */
      WLC_NONULL bool (*button)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint32_t button, enum wlc_button_state, const struct wlc_origin*);

      /** Scroll event was triggered, view handle will be zero if there was no focus. Return true to prevent sending the event to clients. */
      WLC_NONULL bool (*scroll)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint8_t axis_bits, double amount[2]);

      /** Motion event was triggered, view handle will be zero if there was no focus. Apply with wlc_pointer_set_origin to agree. Return true to prevent sending the event to clients. */
      WLC_NONULL bool (*motion)(wlc_handle view, uint32_t time, const struct wlc_origin*);
   } pointer;

   struct {
      /** Touch event was triggered, view handle will be zero if there was no focus. Return true to prevent sending the event to clients. */
      WLC_NONULL bool (*touch)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, enum wlc_touch_type, int32_t slot, const struct wlc_origin*);
   } touch;

   struct {
      /** Compositor is ready to accept clients. */
      void (*ready)(void);
   } compositor;

   /**
    * Experimental input api.
    * libinput isn't abstracted, so no handles given.
    */
   struct {
      /** Input device was created. Return value does nothing. */
      bool (*created)(struct libinput_device *device);

      /** Input device was destroyed. */
      void (*destroyed)(struct libinput_device *device);
   } input;
};

/** -- Core API */

/** Set log handler. Can be set before wlc_init. */
void wlc_log_set_handler(void (*cb)(enum wlc_log_type type, const char *str));

/**
 * Initialize wlc. Returns false on failure.
 *
 * Avoid running unverified code before wlc_init as wlc compositor may be run with higher
 * privileges on non logind systems where compositor binary needs to be suid.
 *
 * wlc_init's purpose is to initialize and drop privileges as soon as possible.
 *
 * You can pass argc and argv from main(), so wlc can rename the process it forks
 * to cleanup crashed parent process and do FD passing (non-logind).
 */
WLC_NONULLV(1) bool wlc_init(const struct wlc_interface *interface, int argc, char *argv[]);

/** Terminate wlc. */
void wlc_terminate(void);

/** Query backend wlc is using. */
enum wlc_backend_type wlc_get_backend_type(void);

/** Exec program. */
WLC_NONULLV(1) void wlc_exec(const char *bin, char *const args[]);

/** Run event loop. */
void wlc_run(void);

/** Link custom data to handle. */
void wlc_handle_set_user_data(wlc_handle handle, const void *userdata);

/** Get linked custom data from handle. */
void* wlc_handle_get_user_data(wlc_handle handle);

/** Add fd to event loop. Return value of callback is unused, you should return 0. */
WLC_NONULLV(3) struct wlc_event_source* wlc_event_loop_add_fd(int fd, uint32_t mask, int (*cb)(int fd, uint32_t mask, void *arg), void *arg);

/** Add timer to event loop. Return value of callback is unused, you should return 0. */
WLC_NONULLV(1) struct wlc_event_source* wlc_event_loop_add_timer(int (*cb)(void *arg), void *arg);

/** Update timer to trigger after delay. Returns true on success. */
WLC_NONULL bool wlc_event_source_timer_update(struct wlc_event_source *source, int32_t ms_delay);

/** Remove event source from event loop. */
WLC_NONULL void wlc_event_source_remove(struct wlc_event_source *source);

/** -- Output API */

/** Get outputs. Returned array is a direct reference, careful when moving and destroying handles. */
const wlc_handle* wlc_get_outputs(size_t *out_memb);

/** Get focused output. */
wlc_handle wlc_get_focused_output(void);

/** Get output name. */
const char* wlc_output_get_name(wlc_handle output);

/** Get connector type. */
enum wlc_connector_type wlc_output_get_connector_type(wlc_handle output);

/** Get connector id. */
uint32_t wlc_output_get_connector_id(wlc_handle output);

/** Get sleep state. */
bool wlc_output_get_sleep(wlc_handle output);

/** Wake up / sleep. */
void wlc_output_set_sleep(wlc_handle output, bool sleep);

/** Get resolution. */
const struct wlc_size* wlc_output_get_resolution(wlc_handle output);

/** Set resolution. */
WLC_NONULL void wlc_output_set_resolution(wlc_handle output, const struct wlc_size *resolution);

/** Get current visibility bitmask. */
uint32_t wlc_output_get_mask(wlc_handle output);

/** Set visibility bitmask. */
void wlc_output_set_mask(wlc_handle output, uint32_t mask);

/** Get pixels. If you return true in callback, the rgba data will be not freed. Do this if you don't want to copy the buffer. */
WLC_NONULL void wlc_output_get_pixels(wlc_handle output, bool (*pixels)(const struct wlc_size *size, uint8_t *rgba, void *arg), void *arg);

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

/** Set geometry. Set edges if the geometry change is caused by interactive resize. */
WLC_NONULL void wlc_view_set_geometry(wlc_handle view, uint32_t edges, const struct wlc_geometry *geometry);

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

/** Get class. (shell-surface only) */
const char* wlc_view_get_class(wlc_handle view);

/** Get app id. (xdg-surface only) */
const char* wlc_view_get_app_id(wlc_handle view);

/** --  Input API
 * Very recent stuff, things may change.
 * XXX: This api is dumb and assumes there is only single xkb state and keymap.
 *      In case of multiple keyboards, we want to each keyboard have own state and layout.
 *      Thus we need wlc_handle for keyboards eventually. */

/** Internal xkb_state exposed. You can use it to do more advanced key handling.
 *  However you should avoid messing up with its state. */
struct xkb_state* wlc_keyboard_get_xkb_state(void);

/** Internal xkb_keymap exposed. You can use it to do more advanced key handling. */
struct xkb_keymap* wlc_keyboard_get_xkb_keymap(void);

/** Get currently held keys. */
const uint32_t* wlc_keyboard_get_current_keys(size_t *out_memb);

/** Utility function to convert raw keycode to keysym. Passed modifiers may transform the key. */
uint32_t wlc_keyboard_get_keysym_for_key(uint32_t key, const struct wlc_modifiers *modifiers);

/** Utility function to convert raw keycode to Unicode/UTF-32 codepoint. Passed modifiers may transform the key. */
uint32_t wlc_keyboard_get_utf32_for_key(uint32_t key, const struct wlc_modifiers *modifiers);

/** Get current pointer origin. */
void wlc_pointer_get_origin(struct wlc_origin *out_origin);

/** Set current pointer origin. */
void wlc_pointer_set_origin(struct wlc_origin new_origin);

#endif /* _WLC_H_ */
