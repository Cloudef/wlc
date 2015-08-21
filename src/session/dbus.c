#include <stdbool.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/eventfd.h>
#include <wayland-server.h>
#include <chck/string/string.h>
#include "internal.h"
#include "dbus.h"

static struct {
   void *handle;
   DBusConnection* (*dbus_bus_get_private)(DBusBusType, DBusError*);
   void (*dbus_bus_add_match)(DBusConnection*, const char*, DBusError*);
   void (*dbus_bus_remove_match)(DBusConnection*, const char*, DBusError*);
   dbus_bool_t (*dbus_connection_set_timeout_functions)(DBusConnection*, DBusAddTimeoutFunction, DBusRemoveTimeoutFunction, DBusTimeoutToggledFunction, void*, DBusFreeFunction);
   dbus_bool_t (*dbus_connection_set_watch_functions)(DBusConnection*, DBusAddWatchFunction, DBusRemoveWatchFunction, DBusWatchToggledFunction, void*, DBusFreeFunction);
   void (*dbus_connection_set_exit_on_disconnect)(DBusConnection*, dbus_bool_t);
   void (*dbus_connection_set_change_sigpipe)(dbus_bool_t);
   DBusConnection* (*dbus_connection_ref)(DBusConnection*);
   void (*dbus_connection_unref)(DBusConnection*);
   void (*dbus_connection_close)(DBusConnection*);
   DBusDispatchStatus (*dbus_connection_dispatch)(DBusConnection*);
   void* (*dbus_timeout_get_data)(DBusTimeout*);
   dbus_bool_t (*dbus_timeout_get_enabled)(DBusTimeout*);
   dbus_bool_t (*dbus_timeout_handle)(DBusTimeout*);
   int (*dbus_timeout_get_interval)(DBusTimeout*);
   void (*dbus_timeout_set_data)(DBusTimeout*, void*, DBusFreeFunction);
   unsigned int (*dbus_watch_get_flags)(DBusWatch*);
   dbus_bool_t (*dbus_watch_get_enabled)(DBusWatch*);
   void (*dbus_watch_set_data)(DBusWatch*, void*, DBusFreeFunction);
   void* (*dbus_watch_get_data)(DBusWatch*);
   dbus_bool_t (*dbus_watch_handle)(DBusWatch*, unsigned int);
   int (*dbus_watch_get_unix_fd)(DBusWatch*);
   void (*dbus_error_init)(DBusError*);
   void (*dbus_error_free)(DBusError*);
   dbus_bool_t (*dbus_error_is_set)(const DBusError*);
} dbus;

static bool
dbus_load(void)
{
   if (dbus.handle)
      return true;

   const char *lib = "libdbus-1.so", *func = NULL;

   if (!(dbus.handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (dbus.x = dlsym(dbus.handle, (func = #x)))

   if (!load(dbus_bus_get_private))
      goto function_pointer_exception;
   if (!load(dbus_bus_add_match))
      goto function_pointer_exception;
   if (!load(dbus_bus_remove_match))
      goto function_pointer_exception;
   if (!load(dbus_connection_set_timeout_functions))
      goto function_pointer_exception;
   if (!load(dbus_connection_set_watch_functions))
      goto function_pointer_exception;
   if (!load(dbus_connection_set_exit_on_disconnect))
      goto function_pointer_exception;
   if (!load(dbus_connection_set_change_sigpipe))
      goto function_pointer_exception;
   if (!load(dbus_connection_ref))
      goto function_pointer_exception;
   if (!load(dbus_connection_unref))
      goto function_pointer_exception;
   if (!load(dbus_connection_close))
      goto function_pointer_exception;
   if (!load(dbus_connection_dispatch))
      goto function_pointer_exception;
   if (!load(dbus_timeout_get_data))
      goto function_pointer_exception;
   if (!load(dbus_timeout_get_enabled))
      goto function_pointer_exception;
   if (!load(dbus_timeout_handle))
      goto function_pointer_exception;
   if (!load(dbus_timeout_get_interval))
      goto function_pointer_exception;
   if (!load(dbus_timeout_set_data))
      goto function_pointer_exception;
   if (!load(dbus_watch_get_flags))
      goto function_pointer_exception;
   if (!load(dbus_watch_get_enabled))
      goto function_pointer_exception;
   if (!load(dbus_watch_set_data))
      goto function_pointer_exception;
   if (!load(dbus_watch_get_data))
      goto function_pointer_exception;
   if (!load(dbus_watch_handle))
      goto function_pointer_exception;
   if (!load(dbus_watch_get_unix_fd))
      goto function_pointer_exception;
   if (!load(dbus_error_init))
      goto function_pointer_exception;
   if (!load(dbus_error_free))
      goto function_pointer_exception;
   if (!load(dbus_error_is_set))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   wlc_log(WLC_LOG_WARN, "Could not load function '%s' from '%s'", func, lib);
   return false;
}

static int
dispatch_watch(int fd, uint32_t mask, void *data)
{
   (void)fd;
   DBusWatch *watch = data;

   if (dbus.dbus_watch_get_enabled(watch)) {
      uint32_t flags = 0;
      if (mask & WL_EVENT_READABLE)
         flags |= DBUS_WATCH_READABLE;
      if (mask & WL_EVENT_WRITABLE)
         flags |= DBUS_WATCH_WRITABLE;
      if (mask & WL_EVENT_HANGUP)
         flags |= DBUS_WATCH_HANGUP;
      if (mask & WL_EVENT_ERROR)
         flags |= DBUS_WATCH_ERROR;

      dbus.dbus_watch_handle(watch, flags);
   }

   return 0;
}

static dbus_bool_t
add_watch(DBusWatch *watch, void *data)
{
   struct wl_event_loop *loop = data;

   uint32_t mask = 0;
   if (dbus.dbus_watch_get_enabled(watch)) {
      uint32_t flags = dbus.dbus_watch_get_flags(watch);
      if (flags & DBUS_WATCH_READABLE)
         mask |= WL_EVENT_READABLE;
      if (flags & DBUS_WATCH_WRITABLE)
         mask |= WL_EVENT_WRITABLE;
   }

   int fd = dbus.dbus_watch_get_unix_fd(watch);
   struct wl_event_source *s;
   if (!(s = wl_event_loop_add_fd(loop, fd, mask, dispatch_watch, watch)))
      return false;

   dbus.dbus_watch_set_data(watch, s, NULL);
   return true;
}

static void
remove_watch(DBusWatch *watch, void *data)
{
   (void)data;
   struct wl_event_source *s;
   if (!(s = dbus.dbus_watch_get_data(watch)))
      return;

   wl_event_source_remove(s);
}

static void
toggle_watch(DBusWatch *watch, void *data)
{
   (void)data;
   struct wl_event_source *s;
   if (!(s = dbus.dbus_watch_get_data(watch)))
      return;

   uint32_t mask = 0;
   if (dbus.dbus_watch_get_enabled(watch)) {
      uint32_t flags = dbus.dbus_watch_get_flags(watch);
      if (flags & DBUS_WATCH_READABLE)
         mask |= WL_EVENT_READABLE;
      if (flags & DBUS_WATCH_WRITABLE)
         mask |= WL_EVENT_WRITABLE;
   }

   wl_event_source_fd_update(s, mask);
}

static int
dispatch_timeout(void *data)
{
   DBusTimeout *timeout = data;
   if (dbus.dbus_timeout_get_enabled(timeout))
      dbus.dbus_timeout_handle(timeout);
   return 0;
}

static int
adjust_timeout(DBusTimeout *timeout, struct wl_event_source *s)
{
   int64_t t = 0;
   if (dbus.dbus_timeout_get_enabled(timeout))
      t = dbus.dbus_timeout_get_interval(timeout);
   return wl_event_source_timer_update(s, t);
}

static dbus_bool_t
add_timeout(DBusTimeout *timeout, void *data)
{
   struct wl_event_loop *loop = data;

   struct wl_event_source *s;
   if (!(s = wl_event_loop_add_timer(loop, dispatch_timeout, timeout)))
      return false;

   if (adjust_timeout(timeout, s) < 0)
      goto error0;

   dbus.dbus_timeout_set_data(timeout, s, NULL);
   return true;

error0:
   wl_event_source_remove(s);
   return false;
}

static void
remove_timeout(DBusTimeout *timeout, void *data)
{
   (void)data;
   struct wl_event_source *s;
   if (!(s = dbus.dbus_timeout_get_data(timeout)))
      return;

   wl_event_source_remove(s);
}

static void
toggle_timeout(DBusTimeout *timeout, void *data)
{
   (void)data;
   struct wl_event_source *s;
   if (!(s = dbus.dbus_timeout_get_data(timeout)))
      return;

   adjust_timeout(timeout, s);
}

static int
dispatch(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;
   DBusConnection *c = data;
   int r;

   do {
      r = dbus.dbus_connection_dispatch(c);
      if (r == DBUS_DISPATCH_COMPLETE)
         r = 0;
      else if (r == DBUS_DISPATCH_DATA_REMAINS)
         r = -EAGAIN;
      else if (r == DBUS_DISPATCH_NEED_MEMORY)
         r = -ENOMEM;
      else
         r = -EIO;
   } while (r == -EAGAIN);

   if (r)
      wlc_log(WLC_LOG_WARN, "cannot dispatch dbus events: %d", r);

   return 0;
}

static bool
dbus_bind(struct wl_event_loop *loop, DBusConnection *c, struct wl_event_source **ctx_out)
{
   assert(ctx_out);
   *ctx_out = NULL;

   /* Idle events cannot reschedule themselves, therefore we use a dummy
    * event-fd and mark it for post-dispatch. Hence, the dbus
    * dispatcher is called after every dispatch-round.
    * This is required as dbus doesn't allow dispatching events from
    * within its own event sources. */

   int fd;
   if ((fd = eventfd(0, EFD_CLOEXEC)) < 0)
      return false;

   *ctx_out = wl_event_loop_add_fd(loop, fd, 0, dispatch, c);
   close(fd);

   if (!*ctx_out)
      return false;

   wl_event_source_check(*ctx_out);

   if (!dbus.dbus_connection_set_watch_functions(c, add_watch, remove_watch, toggle_watch, loop, NULL))
      goto error0;

   if (!dbus.dbus_connection_set_timeout_functions(c, add_timeout, remove_timeout, toggle_timeout, loop, NULL))
      goto error0;

   dbus.dbus_connection_ref(c);
   return true;

error0:
   dbus.dbus_connection_set_timeout_functions(c, NULL, NULL, NULL, NULL, NULL);
   dbus.dbus_connection_set_watch_functions(c, NULL, NULL, NULL, NULL, NULL);
   wl_event_source_remove(*ctx_out);
   *ctx_out = NULL;
   return false;
}

static void
dbus_unbind(DBusConnection *c, struct wl_event_source *ctx)
{
   dbus.dbus_connection_set_timeout_functions(c, NULL, NULL, NULL, NULL, NULL);
   dbus.dbus_connection_set_watch_functions(c, NULL, NULL, NULL, NULL, NULL);
   dbus.dbus_connection_unref(c);
   wl_event_source_remove(ctx);
}

bool
wlc_dbus_add_match(DBusConnection *c, const char *format, ...)
{
   va_list list;
   va_start(list, format);
   struct chck_string str = {0};
   const bool r = chck_string_set_varg(&str, format, list);
   va_end(list);

   if (!r)
      return false;

   DBusError err;
   dbus.dbus_error_init(&err);
   dbus.dbus_bus_add_match(c, str.data, &err);
   chck_string_release(&str);

   if (dbus.dbus_error_is_set(&err)) {
      wlc_log(WLC_LOG_ERROR, "dbus: %s", err.name);
      wlc_log(WLC_LOG_ERROR, "dbus: %s", err.message);
      dbus.dbus_error_free(&err);
      return false;
   }

   return true;
}

bool
wlc_dbus_add_match_signal(DBusConnection *c, const char *sender, const char *iface, const char *member, const char *path)
{
   return wlc_dbus_add_match(c, "type='signal',sender='%s',interface='%s',member='%s',path='%s'", sender, iface, member, path);
}

void
wlc_dbus_remove_match(DBusConnection *c, const char *format, ...)
{
   va_list list;
   va_start(list, format);
   struct chck_string str = {0};
   const bool r = chck_string_set_varg(&str, format, list);
   va_end(list);

   if (r)
      dbus.dbus_bus_remove_match(c, str.data, NULL);

   chck_string_release(&str);
}

void
wlc_dbus_remove_match_signal(DBusConnection *c, const char *sender, const char *iface, const char *member, const char *path)
{
   wlc_dbus_remove_match(c, "type='signal',sender='%s',interface='%s',member='%s',path='%s'", sender, iface, member, path);
}

void
wlc_dbus_close(DBusConnection *c, struct wl_event_source *ctx)
{
   dbus_unbind(c, ctx);
   dbus.dbus_connection_close(c);
   dbus.dbus_connection_unref(c);
}

bool
wlc_dbus_open(struct wl_event_loop *loop, DBusBusType bus, DBusConnection **out, struct wl_event_source **ctx_out)
{
   if (!dbus_load())
      return false;

   dbus.dbus_connection_set_change_sigpipe(false);

   /* This is actually synchronous. It blocks for some authentication and
    * setup. We just trust the dbus-server here and accept this blocking
    * call. There is no real reason to complicate things further and make
    * this asynchronous/non-blocking. A context should be created during
    * thead/process/app setup, so blocking calls should be fine. */

   DBusConnection *c;
   if (!(c = dbus.dbus_bus_get_private(bus, NULL)))
      return false;

   dbus.dbus_connection_set_exit_on_disconnect(c, false);

   if (!dbus_bind(loop, c, ctx_out))
      goto error0;

   *out = c;
   return true;

error0:
   dbus.dbus_connection_close(c);
   dbus.dbus_connection_unref(c);
   return false;
}
