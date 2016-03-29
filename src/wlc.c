#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <chck/string/string.h>
#include "internal.h"
#include "visibility.h"
#include "compositor/compositor.h"
#include "session/tty.h"
#include "session/fd.h"
#include "session/udev.h"
#include "session/logind.h"
#include "xwayland/xwayland.h"
#include "resources/resources.h"

static struct wlc {
   struct wlc_compositor compositor;
   struct wlc_interface interface;
   struct wlc_system_signals signals;
   struct wl_display *display;
   void (*log_fun)(enum wlc_log_type type, const char *str);
   bool active;
} wlc;

static inline void
wl_cb_log(const char *fmt, va_list args)
{
   wlc_vlog(WLC_LOG_WAYLAND, fmt, args);
}

void
wlc_vlog(enum wlc_log_type type, const char *fmt, va_list args)
{
   assert(fmt);

   if (!wlc.log_fun)
      return;

   struct chck_string str = {0};
   if (chck_string_set_varg(&str, fmt, args))
      wlc.log_fun(type, str.data);

   chck_string_release(&str);
}

void
wlc_log(enum wlc_log_type type, const char *fmt, ...)
{
   va_list argp;
   va_start(argp, fmt);
   wlc_vlog(type, fmt, argp);
   va_end(argp);
}

void
wlc_dlog(enum wlc_debug dbg, const char *fmt, ...)
{
   static struct {
      const char *name;
      bool active;
      bool checked;
   } channels[WLC_DBG_LAST] = {
      { "handle", false, false },
      { "render", false, false },
      { "render-loop", false, false },
      { "focus", false, false },
      { "xwm", false, false },
      { "keyboard", false, false },
      { "commit", false, false },
      { "request", false, false },
   };

   if (!channels[dbg].checked) {
      const char *name = channels[dbg].name;
      const char *s = getenv("WLC_DEBUG");
      for (size_t len = strlen(name); s && *s && !chck_cstrneq(s, name, len); s += strcspn(s, ",") + 1);
      channels[dbg].checked = true;
      if (!(channels[dbg].active = (s && *s != 0)))
         return;
   } else if (!channels[dbg].active) {
      return;
   }

   va_list argp;
   va_start(argp, fmt);
   wlc_vlog(WLC_LOG_INFO, fmt, argp);
   va_end(argp);
}

uint32_t
wlc_get_time(struct timespec *out_ts)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   if (out_ts) memcpy(out_ts, &ts, sizeof(ts));
   return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void
wlc_set_active(bool active)
{
   if (active == wlc.active)
      return;

   wlc.active = active;
   struct wlc_activate_event ev = { .active = active, .vt = 0 };
   wl_signal_emit(&wlc.signals.activate, &ev);
   wlc_log(WLC_LOG_INFO, (wlc.active ? "become active" : "deactive"));
}

WLC_PURE bool
wlc_get_active(void)
{
   return wlc.active;
}

WLC_CONST const struct wlc_interface*
wlc_interface(void)
{
   return &wlc.interface;
}

WLC_CONST struct wlc_system_signals*
wlc_system_signals(void)
{
   return &wlc.signals;
}

struct wl_event_loop*
wlc_event_loop(void)
{
   assert(wlc_display());
   return wl_display_get_event_loop(wlc_display());
}

WLC_PURE struct wl_display*
wlc_display(void)
{
   return wlc.display;
}

static void
compositor_event(struct wl_listener *listener, void *data)
{
   (void)listener, (void)data;
   // this event is currently only used for knowing when compositor died
   wl_display_terminate(wlc.display);
}

static struct wl_listener compositor_listener = {
   .notify = compositor_event,
};

void
wlc_cleanup(void)
{
   if (wlc.display) {
      wlc_log(WLC_LOG_INFO, "Cleanup wlc");

      // fd process never allocates display
      wlc_xwayland_terminate();
      wl_display_flush_clients(wlc.display);
      wlc_compositor_release(&wlc.compositor);
      wl_display_flush_clients(wlc.display);
      wl_list_remove(&compositor_listener.link);
      wlc_resources_terminate();
      wlc_input_terminate();
      wlc_udev_terminate();
      wlc_fd_terminate();
   }

   // however if main process crashed, fd process does
   // know enough about tty to reset it.
   wlc_tty_terminate();

   if (wlc.display)
      wl_display_destroy(wlc.display);

   memset(&wlc, 0, sizeof(wlc));
}

WLC_API struct wlc_event_source*
wlc_event_loop_add_fd(int fd, uint32_t mask, int (*cb)(int fd, uint32_t mask, void *userdata), void *userdata)
{
   assert(wlc_event_loop());
   return (struct wlc_event_source*)wl_event_loop_add_fd(wlc_event_loop(), fd, mask, cb, userdata);
}

WLC_API struct wlc_event_source*
wlc_event_loop_add_timer(int (*cb)(void *userdata), void *userdata)
{
   assert(wlc_event_loop());
   return (struct wlc_event_source*)wl_event_loop_add_timer(wlc_event_loop(), cb, userdata);
}

WLC_API bool
wlc_event_source_timer_update(struct wlc_event_source *source, int32_t ms_delay)
{
   assert(source);
   assert(wlc_event_loop());
   return (wl_event_source_timer_update((struct wl_event_source*)source, ms_delay) == 0);
}

WLC_API void
wlc_event_source_remove(struct wlc_event_source *source)
{
   assert(source);

   if (wlc.display)
      wl_event_source_remove((struct wl_event_source*)source);
}

WLC_API void
wlc_log_set_handler(void (*cb)(enum wlc_log_type type, const char *str))
{
   wlc.log_fun = cb;
}

WLC_API void
wlc_exec(const char *bin, char *const args[])
{
   assert(bin);

   if (chck_cstr_is_empty(bin))
      return;

   pid_t p;
   if ((p = fork()) == 0) {
      setsid();
      freopen("/dev/null", "w", stdout);
      freopen("/dev/null", "w", stderr);
      execvp(bin, args);
      _exit(EXIT_FAILURE);
   } else if (p < 0) {
      wlc_log(WLC_LOG_ERROR, "Failed to fork for '%s'", bin);
   }
}

WLC_API void
wlc_run(void)
{
   if (!wlc.display)
      return;

   wlc.compositor.state.ready = false;

   bool emit_ready = true;
   const char *xwayland = getenv("WLC_XWAYLAND");
   if (!xwayland || !chck_cstreq(xwayland, "0"))
      emit_ready = !wlc_xwayland_init();

   // Emit ready immediately when no Xwayland
   if (emit_ready) {
      WLC_INTERFACE_EMIT(compositor.ready);
      wlc.compositor.state.ready = true;
   }

   wlc_set_active(true);

   if (wlc_compositor_is_good(&wlc.compositor))
      wl_display_run(wlc.display);

   wlc_cleanup();
}

WLC_API WLC_PURE enum wlc_backend_type
wlc_get_backend_type(void)
{
   return wlc.compositor.backend.type;
}

WLC_API void
wlc_terminate(void)
{
   if (!wlc.display)
      return;

   wlc_log(WLC_LOG_INFO, "Terminating wlc...");
   wl_signal_emit(&wlc.signals.terminate, NULL);
}

WLC_API bool
wlc_init(void)
{
   if (wlc.display)
      return true;

   // reset wlc state, but keep important data
   {
      void *log_fun = wlc.log_fun;
      struct wlc_interface old_interface = wlc.interface;
      wlc = (struct wlc){0};
      wlc.log_fun = log_fun;
      wlc.interface = old_interface;
   }

   wl_log_set_handler_server(wl_cb_log);

   unsetenv("TERM");
   const char *x11display = getenv("DISPLAY");
   bool privileged = false;
   const bool has_logind = wlc_logind_available();

   if (getuid() != geteuid() || getgid() != getegid()) {
      wlc_log(WLC_LOG_INFO, "Doing work on SUID/SGID side and dropping permissions");
      privileged = true;
   } else if (!x11display && !has_logind && access("/dev/input/event0", R_OK | W_OK) != 0) {
      die("Not running from X11 and no access to /dev/input/event0 or logind unavailable");
   }

   int vt = 0;

#ifdef HAS_LOGIND
   // Init logind if we are not running as SUID.
   // We need event loop for logind to work, and thus we won't allow it on SUID process.
   if (!privileged && !x11display && has_logind) {
      if (!(wlc.display = wl_display_create()))
         die("Failed to create wayland display");

      const char *xdg_seat = getenv("XDG_SEAT");
      if (!(vt = wlc_logind_init(xdg_seat ? xdg_seat : "seat0")))
         die("Failed to init logind");
   }
#else
   (void)privileged;
#endif

   if (!x11display)
      wlc_tty_init(vt);

   // -- we open tty before dropping permissions
   //    so the fd process can also handle cleanup in case of crash
   //    if logind initialized correctly, fd process does nothing but handle crash.

   {
      struct wl_display *display = wlc.display;
      wlc.display = NULL;
      wlc_fd_init((vt != 0));
      wlc.display = display;
   }

   // -- permissions are now dropped

   wl_signal_init(&wlc.signals.terminate);
   wl_signal_init(&wlc.signals.activate);
   wl_signal_init(&wlc.signals.compositor);
   wl_signal_init(&wlc.signals.focus);
   wl_signal_init(&wlc.signals.surface);
   wl_signal_init(&wlc.signals.input);
   wl_signal_init(&wlc.signals.output);
   wl_signal_init(&wlc.signals.render);
   wl_signal_init(&wlc.signals.xwayland);
   wl_signal_add(&wlc.signals.compositor, &compositor_listener);

   if (!wlc_resources_init())
      die("Failed to init resource manager");

   if (!wlc.display && !(wlc.display = wl_display_create()))
      die("Failed to create wayland display");

   const char *socket_name;
   if (!(socket_name = wl_display_add_socket_auto(wlc.display)))
      die("Failed to add socket to wayland display");

   if (socket_name) // shut up static analyze
      setenv("WAYLAND_DISPLAY", socket_name, true);

   if (wl_display_init_shm(wlc.display) != 0)
      die("Failed to init shm");

   if (!wlc_udev_init())
      die("Failed to init udev");

   const char *libinput = getenv("WLC_LIBINPUT");
   if (!x11display || (libinput && !chck_cstreq(libinput, "0"))) {
      if (!wlc_input_init())
         die("Failed to init input");
   }

   if (!wlc_compositor(&wlc.compositor))
      die("Failed to init compositor");

   return true;
}

WLC_API void
wlc_set_output_created_cb(bool (*cb)(wlc_handle output))
{
   wlc.interface.output.created = cb;
}

WLC_API void
wlc_set_output_destroyed_cb(void (*cb)(wlc_handle output))
{
   wlc.interface.output.destroyed = cb;
}

WLC_API void
wlc_set_output_focus_cb(void (*cb)(wlc_handle output, bool focus))
{
   wlc.interface.output.focus = cb;
}

WLC_API void
wlc_set_output_resolution_cb(void (*cb)(wlc_handle output, const struct wlc_size *from, const struct wlc_size *to))
{
   wlc.interface.output.resolution = cb;
}

WLC_API void
wlc_set_output_render_pre_cb(void (*cb)(wlc_handle output))
{
   wlc.interface.output.render.pre = cb;
}

WLC_API void
wlc_set_output_render_post_cb(void (*cb)(wlc_handle output))
{
   wlc.interface.output.render.post = cb;
}

WLC_API void
wlc_set_output_context_created_cb(void (*cb)(wlc_handle output))
{
   wlc.interface.output.context.created = cb;
}

WLC_API void
wlc_set_output_context_destroyed_cb(void (*cb)(wlc_handle output))
{
   wlc.interface.output.context.destroyed = cb;
}

WLC_API void
wlc_set_view_created_cb(bool (*cb)(wlc_handle view))
{
   wlc.interface.view.created = cb;
}

WLC_API void
wlc_set_view_destroyed_cb(void (*cb)(wlc_handle view))
{
   wlc.interface.view.destroyed = cb;
}

WLC_API void
wlc_set_view_focus_cb(void (*cb)(wlc_handle view, bool focus))
{
   wlc.interface.view.focus = cb;
}

WLC_API void
wlc_set_view_move_to_output_cb(void (*cb)(wlc_handle view, wlc_handle from_output, wlc_handle to_output))
{
   wlc.interface.view.move_to_output = cb;
}

WLC_API void
wlc_set_view_request_geometry_cb(void (*cb)(wlc_handle view, const struct wlc_geometry*))
{
   wlc.interface.view.request.geometry = cb;
}

WLC_API void
wlc_set_view_request_state_cb(void (*cb)(wlc_handle view, enum wlc_view_state_bit, bool toggle))
{
   wlc.interface.view.request.state = cb;
}

WLC_API void
wlc_set_view_request_move_cb(void (*cb)(wlc_handle view, const struct wlc_point*))
{
   wlc.interface.view.request.move = cb;
}

WLC_API void
wlc_set_view_request_resize_cb(void (*cb)(wlc_handle view, uint32_t edges, const struct wlc_point*))
{
   wlc.interface.view.request.resize = cb;
}

WLC_API void
wlc_set_view_render_pre_cb(void (*cb)(wlc_handle view))
{
   wlc.interface.view.render.pre = cb;
}

WLC_API void
wlc_set_view_render_post_cb(void (*cb)(wlc_handle view))
{
   wlc.interface.view.render.post = cb;
}

WLC_API void
wlc_set_view_properties_updated_cb(void (*cb)(wlc_handle view, uint32_t mask))
{
   wlc.interface.view.properties_updated = cb;
}

WLC_API void
wlc_set_keyboard_key_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint32_t key, enum wlc_key_state))
{
   wlc.interface.keyboard.key = cb;
}

WLC_API void
wlc_set_pointer_button_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint32_t button, enum wlc_button_state, const struct wlc_point*))
{
   wlc.interface.pointer.button = cb;
}

WLC_API void
wlc_set_pointer_scroll_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, uint8_t axis_bits, double amount[2]))
{
   wlc.interface.pointer.scroll = cb;
}

WLC_API void
wlc_set_pointer_motion_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_point*))
{
   wlc.interface.pointer.motion = cb;
}

WLC_API void
wlc_set_touch_cb(bool (*cb)(wlc_handle view, uint32_t time, const struct wlc_modifiers*, enum wlc_touch_type, int32_t slot, const struct wlc_point*))
{
   wlc.interface.touch.touch = cb;
}

WLC_API void
wlc_set_swipe_cb(bool (*cb)(wlc_handle view, uint32_t time, int finger_count, const struct wlc_point*, const struct wlc_point*))
{
   wlc.interface.gesture.swipe = cb;
}

WLC_API void
wlc_set_pinch_cb(bool (*cb)(wlc_handle view, uint32_t time, int finger_count, const struct wlc_point*, const struct wlc_point*, double angle, double scale))
{
   wlc.interface.gesture.pinch = cb;
}

WLC_API void
wlc_set_compositor_ready_cb(void (*cb)(void))
{
   wlc.interface.compositor.ready = cb;
}

WLC_API void
wlc_set_compositor_terminate_cb(void (*cb)(void))
{
   wlc.interface.compositor.terminate = cb;
}

WLC_API void
wlc_set_input_created_cb(bool (*cb)(struct libinput_device *device))
{
   wlc.interface.input.created = cb;
}

WLC_API void
wlc_set_input_destroyed_cb(void (*cb)(struct libinput_device *device))
{
   wlc.interface.input.destroyed = cb;
}
