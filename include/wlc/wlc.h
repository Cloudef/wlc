#ifndef _WLC_H_
#define _WLC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <wlc/defines.h>
#include <wlc/geometry.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <sys/types.h>

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
   WLC_BACKEND_WAYLAND,
};

/** mask in wlc_event_loop_add_fd(); */
enum wlc_event_bit {
   WLC_EVENT_READABLE = 0x01,
   WLC_EVENT_WRITABLE = 0x02,
   WLC_EVENT_HANGUP = 0x04,
   WLC_EVENT_ERROR = 0x08,
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

/** wlc_set_view_properties_updated_cb(); */
enum wlc_view_property_update_bit {
   WLC_BIT_PROPERTY_TITLE = 1<<0,
   WLC_BIT_PROPERTY_CLASS = 1<<1,
   WLC_BIT_PROPERTY_APP_ID = 1<<2,
   WLC_BIT_PROPERTY_PID = 1<<3,
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

/** wlc_view_positioner_get_anchor(); */
enum wlc_positioner_anchor_bit {
   WLC_BIT_ANCHOR_NONE = 0,
   WLC_BIT_ANCHOR_TOP = 1<<0,
   WLC_BIT_ANCHOR_BOTTOM = 1<<1,
   WLC_BIT_ANCHOR_LEFT = 1<<2,
   WLC_BIT_ANCHOR_RIGHT = 1<<3
};

/** wlc_view_positioner_get_gravity(); */
enum wlc_positioner_gravity_bit {
   WLC_BIT_GRAVITY_NONE = 0,
   WLC_BIT_GRAVITY_TOP = 1<<0,
   WLC_BIT_GRAVITY_BOTTOM = 1<<1,
   WLC_BIT_GRAVITY_LEFT = 1<<2,
   WLC_BIT_GRAVITY_RIGHT = 1<<3
};

/** wlc_view_positioner_get_gravity(); */
enum wlc_positioner_constraint_adjustment_bit {
   WLC_BIT_CONSTRAINT_ADJUSTMENT_NONE = 0,
   WLC_BIT_CONSTRAINT_ADJUSTMENT_SLIDE_X = 1<<0,
   WLC_BIT_CONSTRAINT_ADJUSTMENT_SLIDE_Y = 1<<1,
   WLC_BIT_CONSTRAINT_ADJUSTMENT_FLIP_X = 1<<2,
   WLC_BIT_CONSTRAINT_ADJUSTMENT_FLIP_Y = 1<<3,
   WLC_BIT_CONSTRAINT_ADJUSTMENT_RESIZE_X = 1<<4,
   WLC_BIT_CONSTRAINT_ADJUSTMENT_RESIZE_Y = 1<<5
};

/** State of keyboard modifiers in various functions. */
struct wlc_modifiers {
   uint32_t leds, mods;
};

/** Dispaly mode settings **/
struct wlc_output_mode {
   int32_t refresh;
   int32_t width, height;
   uint32_t flags; // WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED
};

/** -- Callbacks API */

/** Output was created. Return false if you want to destroy the output. (e.g. failed to allocate data related to view) */
void wlc_set_output_created_cb(bool (*cb)(wlc_handle output));

/** Output was destroyed. */
void wlc_set_output_destroyed_cb(void (*cb)(wlc_handle output));

/** Output got or lost focus. */
void wlc_set_output_focus_cb(void (*cb)(wlc_handle output, bool focus));

/** Output resolution changed. */
void wlc_set_output_resolution_cb(void (*cb)(wlc_handle output, const struct wlc_size *from, const struct wlc_size *to));

/** Output pre render hook. */
void wlc_set_output_render_pre_cb(void (*cb)(wlc_handle output));

/** Output post render hook. */
void wlc_set_output_render_post_cb(void (*cb)(wlc_handle output));

/** Output context is created. This generally happens on startup and when current tty changes */
void wlc_set_output_context_created_cb(void (*cb)(wlc_handle output));

/** Output context was destroyed. */
void wlc_set_output_context_destroyed_cb(void (*cb)(wlc_handle output));

/** View was created. Return false if you want to destroy the view. (e.g. failed to allocate data related to view) */
void wlc_set_view_created_cb(bool (*cb)(wlc_handle view));

/** View was destroyed. */
void wlc_set_view_destroyed_cb(void (*cb)(wlc_handle view));

/** View got or lost focus. */
void wlc_set_view_focus_cb(void (*cb)(wlc_handle view, bool focus));

/** View was moved to output. */
void wlc_set_view_move_to_output_cb(void (*cb)(wlc_handle view, wlc_handle from_output, wlc_handle to_output));

/** Request to set given geometry for view. Apply using wlc_view_set_geometry to agree. */
void wlc_set_view_request_geometry_cb(void (*cb)(wlc_handle view, const struct wlc_geometry*));

/** Request to disable or enable the given state for view. Apply using wlc_view_set_state to agree. */
void wlc_set_view_request_state_cb(void (*cb)(wlc_handle view, enum wlc_view_state_bit, bool toggle));

/** Request to move itself. Start a interactive move to agree. */
void wlc_set_view_request_move_cb(void (*cb)(wlc_handle view, const struct wlc_point*));

/** Request to resize itself with the given edges. Start a interactive resize to agree. */
void wlc_set_view_request_resize_cb(void (*cb)(wlc_handle view, uint32_t edges, const struct wlc_point*));

/** View pre render hook. */
void wlc_set_view_render_pre_cb(void (*cb)(wlc_handle view));

/** View post render hook. */
void wlc_set_view_render_post_cb(void (*cb)(wlc_handle view));

/** View properties (title, class, app_id) was updated */
void wlc_set_view_properties_updated_cb(void (*cb)(wlc_handle view, uint32_t mask));

/**
* View requested to be minimized. Return true if you allow the request, false if not.
* Actual minimization is compositor dependent.
*/
void wlc_set_view_minimized_cb(bool (*cb)(wlc_handle view, bool minimized));

/** Key event was triggered, view handle will be zero if there was no focus. Return true to prevent sending the event to clients. */
void wlc_set_keyboard_key_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint32_t key, enum wlc_key_state));

/** Button event was triggered, view handle will be zero if there was no focus. Return true to prevent sending the event to clients. */
void wlc_set_pointer_button_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint32_t button, enum wlc_button_state, const struct wlc_point*));

/** Scroll event was triggered, view handle will be zero if there was no focus. Return true to prevent sending the event to clients. */
void wlc_set_pointer_scroll_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint8_t axis_bits, double amount[2]));

/** Motion event was triggered, view handle will be zero if there was no focus. Apply with wlc_pointer_set_position to agree. Return true to prevent sending the event to clients. */
void wlc_set_pointer_motion_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_point*));

/** Touch event was triggered, view handle will be zero if there was no focus. Return true to prevent sending the event to clients. */
void wlc_set_touch_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, enum wlc_touch_type, int32_t slot, const struct wlc_point*));

/** Compositor is ready to accept clients. */
void wlc_set_compositor_ready_cb(void (*cb)(void));

/** Compositor is about to terminate */
void wlc_set_compositor_terminate_cb(void (*cb)(void));

/** Input device was created. Return value does nothing. (Experimental) */
void wlc_set_input_created_cb(bool (*cb)(struct libinput_device *device));

/** Input device was destroyed. (Experimental) */
void wlc_set_input_destroyed_cb(void (*cb)(struct libinput_device *device));

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
 * Callbacks should be set using wlc_set_*_cb functions before calling wlc_init,
 * failing to do so will cause any callback the init may trigger to not be called.
 */
bool wlc_init(void);

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
WLC_NONULLV(3) struct wlc_event_source* wlc_event_loop_add_fd(int fd, uint32_t mask, int (*cb)(int fd, uint32_t mask, void *userdata), void *userdata);

/** Add timer to event loop. Return value of callback is unused, you should return 0. */
WLC_NONULLV(1) struct wlc_event_source* wlc_event_loop_add_timer(int (*cb)(void *userdata), void *userdata);

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

/** Get sleep state. */
bool wlc_output_get_sleep(wlc_handle output);

/** Wake up / sleep. */
void wlc_output_set_sleep(wlc_handle output, bool sleep);

/** Set gamma. R, G, and B are color ramp arrays of size elements. */
void wlc_output_set_gamma(wlc_handle output, uint16_t size, uint16_t *r, uint16_t *g, uint16_t *b);

/** Get gamma size */
uint16_t wlc_output_get_gamma_size(wlc_handle output);

/**
 * Get real resolution.
 * Resolution applied by either wlc_output_set_resolution call or initially.
 * Do not use this for coordinate boundary.
 */
const struct wlc_size* wlc_output_get_resolution(wlc_handle output);

/**
 * Get virtual resolution.
 * Resolution with transformations applied for proper rendering for example on high density displays.
 * Use this to figure out coordinate boundary.
 */
const struct wlc_size* wlc_output_get_virtual_resolution(wlc_handle output);

/** Set resolution. */
WLC_NONULL void wlc_output_set_resolution(wlc_handle output, const struct wlc_size *resolution, uint32_t scale);

/** Get scale factor. */
uint32_t wlc_output_get_scale(wlc_handle output);

/** Get current visibility bitmask. */
uint32_t wlc_output_get_mask(wlc_handle output);

/** Set visibility bitmask. */
void wlc_output_set_mask(wlc_handle output, uint32_t mask);

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

/** Get output modes */
const struct wlc_output_mode* wlc_output_get_modes(wlc_handle output, size_t *out_memb);

/**
 * Set output modes. Returns false on failure.
 * The parameter mode_index is the index of the results retrieved from wlc_output_get_modes.
 */
bool wlc_output_set_mode(wlc_handle output, uint32_t mode_index);

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

/** Get current geometry. (what client sees) */
const struct wlc_geometry* wlc_view_get_geometry(wlc_handle view);

/**
 * Get size requested by positioner, as defined in xdg-shell v6.
 * Returns NULL if view has no valid positioner
 */
const struct wlc_size* wlc_view_positioner_get_size(wlc_handle view);

/**
 * Get anchor rectangle requested by positioner, as defined in xdg-shell v6.
 * Returns NULL if view has no valid positioner.
 */
const struct wlc_geometry* wlc_view_positioner_get_anchor_rect(wlc_handle view);

/**
 * Get offset requested by positioner, as defined in xdg-shell v6.
 * Returns NULL if view has no valid positioner,
 * or default value (0, 0) if positioner has no offset set.
 */
const struct wlc_point* wlc_view_positioner_get_offset(wlc_handle view);

/**
 * Get anchor requested by positioner, as defined in xdg-shell v6.
 * Returns default value WLC_BIT_ANCHOR_NONE if view has no valid positioner
 * or if positioner has no anchor set.
 */
enum wlc_positioner_anchor_bit wlc_view_positioner_get_anchor(wlc_handle view);

/**
 * Get anchor requested by positioner, as defined in xdg-shell v6.
 * Returns default value WLC_BIT_GRAVITY_NONE if view has no valid positioner
 * or if positioner has no gravity set.
 */
enum wlc_positioner_gravity_bit wlc_view_positioner_get_gravity(wlc_handle view);

/**
 * Get constraint adjustment requested by positioner, as defined in xdg-shell v6.
 * Returns default value WLC_BIT_CONSTRAINT_ADJUSTMENT_NONE if view has no
 * valid positioner or if positioner has no constraint adjustment set.
 */
enum wlc_positioner_constraint_adjustment_bit wlc_view_positioner_get_constraint_adjustment(wlc_handle view);

/** Get visible geometry. (what wlc displays) */
void wlc_view_get_visible_geometry(wlc_handle view, struct wlc_geometry *out_geometry);

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

/** Get instance. (shell-surface only) */
const char* wlc_view_get_instance(wlc_handle view);

/** Get class. (shell-surface only) */
const char* wlc_view_get_class(wlc_handle view);

/** Get app id. (xdg-surface only) */
const char* wlc_view_get_app_id(wlc_handle view);

/** Get pid. */
pid_t wlc_view_get_pid(wlc_handle view);

/** Returns whether view is minimized or not */
bool wlc_view_is_minimized(wlc_handle view);

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

/** Get current pointer position. */
void wlc_pointer_get_position(struct wlc_point *out_position);

/** Set current pointer position. */
void wlc_pointer_set_position(const struct wlc_point *position);

#ifdef __cplusplus
}
#endif

#endif /* _WLC_H_ */
