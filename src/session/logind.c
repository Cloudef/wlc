/** adapted from weston logind-util.c */

#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <chck/string/string.h>

#if SYSTEMD_FOUND
   #include <systemd/sd-login.h>
#elif ELOGIND_FOUND
   #include <elogind/sd-login.h>
#endif

#include "internal.h"
#include "dbus.h"
#include "logind.h"

#ifndef DRM_MAJOR
#  define DRM_MAJOR 226
#endif

#ifndef KDSKBMUTE
#  define KDSKBMUTE 0x4B51
#endif

static struct {
   char *seat;
   char *sid;
   DBusConnection *dbus;
   struct wl_event_source *dbus_ctx;
   struct chck_string spath;
   int vt;

   struct {
      DBusPendingCall *active;
   } pending;
} logind;

static int
take_device(uint32_t major, uint32_t minor, bool *out_paused)
{
   if (out_paused)
      *out_paused = false;

   DBusMessage *m;
   if (!(m = dbus_message_new_method_call("org.freedesktop.login1", logind.spath.data, "org.freedesktop.login1.Session", "TakeDevice")))
      return -1;

   if (!dbus_message_append_args(m, DBUS_TYPE_UINT32, &major, DBUS_TYPE_UINT32, &minor, DBUS_TYPE_INVALID))
      goto error0;

   DBusMessage *reply;
   if (!(reply = dbus_connection_send_with_reply_and_block(logind.dbus, m, -1, NULL)))
      goto error0;

   int fd;
   dbus_bool_t paused;
   if (!dbus_message_get_args(reply, NULL, DBUS_TYPE_UNIX_FD, &fd, DBUS_TYPE_BOOLEAN, &paused, DBUS_TYPE_INVALID))
      goto error1;

   if (out_paused)
      *out_paused = paused;

   int fl;
   if ((fl = fcntl(fd, F_GETFL)) < 0 || fcntl(fd, F_SETFD, fl | FD_CLOEXEC) < 0)
      goto error2;

   dbus_message_unref(reply);
   dbus_message_unref(m);
   return fd;

error2:
   close(fd);
error1:
   dbus_message_unref(reply);
error0:
   dbus_message_unref(m);
   return -1;
}

static void
release_device(uint32_t major, uint32_t minor)
{
   DBusMessage *m;
   if (!(m = dbus_message_new_method_call("org.freedesktop.login1", logind.spath.data, "org.freedesktop.login1.Session", "ReleaseDevice")))
      return;

   if (dbus_message_append_args(m, DBUS_TYPE_UINT32, &major, DBUS_TYPE_UINT32, &minor, DBUS_TYPE_INVALID))
      dbus_connection_send(logind.dbus, m, NULL);

   dbus_message_unref(m);
}

static void
pause_device(uint32_t major, uint32_t minor)
{
   DBusMessage *m;
   if (!(m = dbus_message_new_method_call("org.freedesktop.login1", logind.spath.data, "org.freedesktop.login1.Session", "PauseDeviceComplete")))
      return;

   if (dbus_message_append_args(m, DBUS_TYPE_UINT32, &major, DBUS_TYPE_UINT32, &minor, DBUS_TYPE_INVALID))
      dbus_connection_send(logind.dbus, m, NULL);

   dbus_message_unref(m);
}

int
wlc_logind_open(const char *path, int flags)
{
   assert(path);
   (void)flags; // unused

   struct stat st;
   if (stat(path, &st) < 0 || !S_ISCHR(st.st_mode))
      return -1;

   int fd;
   if ((fd = take_device(major(st.st_rdev), minor(st.st_rdev), NULL)) < 0)
      return fd;

   return fd;
}

void
wlc_logind_close(int fd)
{
   struct stat st;
   if (fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode))
      return;

   release_device(major(st.st_rdev), minor(st.st_rdev));
}

static void
parse_active(DBusMessage *m, DBusMessageIter *iter)
{
   (void)m;
   assert(m && iter);

   if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_VARIANT)
      return;

   DBusMessageIter sub;
   dbus_message_iter_recurse(iter, &sub);

   if (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_BOOLEAN)
      return;

   dbus_bool_t b;
   dbus_message_iter_get_basic(&sub, &b);
   wlc_set_active(b);
}

static void
get_active_cb(DBusPendingCall *pending, void *data)
{
   assert(pending);
   (void)data;

   dbus_pending_call_unref(logind.pending.active);
   logind.pending.active = NULL;

   DBusMessage *m;
   if (!(m = dbus_pending_call_steal_reply(pending)))
      return;

   DBusMessageIter iter;
   if (dbus_message_get_type(m) == DBUS_MESSAGE_TYPE_METHOD_RETURN && dbus_message_iter_init(m, &iter))
      parse_active(m, &iter);

   dbus_message_unref(m);
}

static void
get_active(void)
{
   DBusMessage *m;
   if (!(m = dbus_message_new_method_call("org.freedesktop.login1", logind.spath.data, "org.freedesktop.DBus.Properties", "Get")))
      return;

   const char *iface = "org.freedesktop.login1.Session";
   const char *name = "Active";
   if (!dbus_message_append_args(m, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
      goto error0;

   DBusPendingCall *pending;
   if (!dbus_connection_send_with_reply(logind.dbus, m, &pending, -1))
      goto error0;

   if (!dbus_pending_call_set_notify(pending, get_active_cb, NULL, NULL))
      goto error1;

   if (logind.pending.active) {
      dbus_pending_call_cancel(logind.pending.active);
      dbus_pending_call_unref(logind.pending.active);
   }

   logind.pending.active = pending;
   return;

error1:
   dbus_pending_call_cancel(pending);
   dbus_pending_call_unref(pending);
error0:
   dbus_message_unref(m);
}

static void
disconnected_dbus(void)
{
   wlc_log(WLC_LOG_INFO, "logind: dbus connection lost, terminating...");
   wlc_terminate();
}

static void
session_removed(DBusMessage *m)
{
   assert(m);

   const char *name, *obj;
   if (!dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_OBJECT_PATH, &obj, DBUS_TYPE_INVALID))
      return;

   if (!chck_cstreq(name, logind.sid))
      return;

   wlc_log(WLC_LOG_INFO, "logind: session closed, terminating...");
   wlc_terminate();
}

static void
property_changed(DBusMessage *m)
{
   assert(m);

   DBusMessageIter iter;
   if (!dbus_message_iter_init(m, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
      goto error0;

   const char *interface;
   dbus_message_iter_get_basic(&iter, &interface);

   if (!dbus_message_iter_next(&iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
      goto error0;

   DBusMessageIter sub;
   dbus_message_iter_recurse(&iter, &sub);

   DBusMessageIter entry;
   while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_DICT_ENTRY) {
      dbus_message_iter_recurse(&sub, &entry);

      if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
         goto error0;

      const char *name;
      dbus_message_iter_get_basic(&entry, &name);
      if (!dbus_message_iter_next(&entry))
         goto error0;

      if (chck_cstreq(name, "Active")) {
         parse_active(m, &entry);
         return;
      }

      dbus_message_iter_next(&sub);
   }

   if (!dbus_message_iter_next(&iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
      goto error0;

   dbus_message_iter_recurse(&iter, &sub);

   while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING) {
      const char *name;
      dbus_message_iter_get_basic(&sub, &name);

      if (chck_cstreq(name, "Active")) {
         get_active();
         return;
      }

      dbus_message_iter_next(&sub);
   }

   return;

error0:
   wlc_log(WLC_LOG_WARN, "logind: cannot parse PropertiesChanged dbus signal");
}

static void
device_paused(DBusMessage *m)
{
   assert(m);

   const char *type;
   uint32_t major, minor;
   if (!dbus_message_get_args(m, NULL, DBUS_TYPE_UINT32, &major, DBUS_TYPE_UINT32, &minor, DBUS_TYPE_STRING, &type, DBUS_TYPE_INVALID))
      return;

   if (chck_cstreq(type, "pause"))
      pause_device(major, minor);

   if (major == DRM_MAJOR)
      wlc_set_active(false);
}

static void
device_resumed(DBusMessage *m)
{
   assert(m);

   uint32_t major;
   if (!dbus_message_get_args(m, NULL, DBUS_TYPE_UINT32, &major, DBUS_TYPE_INVALID))
      return;

   if (major == DRM_MAJOR)
      wlc_set_active(true);
}

static DBusHandlerResult
filter_dbus(DBusConnection *c, DBusMessage *m, void *data)
{
   assert(m);
   (void)c, (void)data;

   if (dbus_message_is_signal(m, DBUS_INTERFACE_LOCAL, "Disconnected")) {
      disconnected_dbus();
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
   }

   struct {
      const char *path, *name;
      void (*function)(DBusMessage *m);
   } map[] = {
      { "org.freedesktop.login1.Manager", "SessionRemoved", session_removed },
      { "org.freedesktop.DBus.Properties", "PropertiesChanged", property_changed },
      { "org.freedesktop.login1.Session", "PauseDevice", device_paused },
      { "org.freedesktop.login1.Session", "ResumeDevice", device_resumed },
      { NULL, NULL, NULL },
   };

   for (uint32_t i = 0; map[i].function; ++i) {
      if (!dbus_message_is_signal(m, map[i].path, map[i].name))
         continue;

      map[i].function(m);
      break;
   }

   return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static bool
setup_dbus(void)
{
   if (!chck_string_set_format(&logind.spath, "/org/freedesktop/login1/session/%s", logind.sid))
      return false;

   if (!dbus_connection_add_filter(logind.dbus, filter_dbus, NULL, NULL))
      goto error0;

   if (!wlc_dbus_add_match_signal(logind.dbus, "org.freedesktop.login1", "org.freedesktop.login1.Manager", "SessionRemoved", "/org/freedesktop/login1"))
      goto error0;

   if (!wlc_dbus_add_match_signal(logind.dbus, "org.freedesktop.login1", "org.freedesktop.login1.Session", "PauseDevice", logind.spath.data))
      goto error0;

   if (!wlc_dbus_add_match_signal(logind.dbus, "org.freedesktop.login1", "org.freedesktop.login1.Session", "ResumeDevice", logind.spath.data))
      goto error0;

   if (!wlc_dbus_add_match_signal(logind.dbus, "org.freedesktop.login1", "org.freedesktop.DBus.Properties", "PropertiesChanged", logind.spath.data))
      goto error0;

   return true;

error0:
   chck_string_release(&logind.spath);
   return false;
}

static bool
take_control(void)
{
   DBusError err;
   dbus_error_init(&err);

   DBusMessage *m;
   if (!(m = dbus_message_new_method_call("org.freedesktop.login1", logind.spath.data, "org.freedesktop.login1.Session", "TakeControl")))
      return false;

   if (!dbus_message_append_args(m, DBUS_TYPE_BOOLEAN, (dbus_bool_t[]){false}, DBUS_TYPE_INVALID))
      goto error0;

   DBusMessage *reply;
   if (!(reply = dbus_connection_send_with_reply_and_block(logind.dbus, m, -1, &err))) {
      if (dbus_error_has_name(&err, DBUS_ERROR_UNKNOWN_METHOD)) {
         wlc_log(WLC_LOG_WARN, "logind: old systemd version detected");
      } else {
         wlc_log(WLC_LOG_WARN, "logind: cannot take control over session %s", logind.sid);
      }
      dbus_error_free(&err);
      goto error0;
   }

   dbus_message_unref(reply);
   dbus_message_unref(m);
   return true;

error0:
   dbus_message_unref(m);
   return false;
}

static void
release_control(void)
{
   if (!logind.spath.data)
      return;

   DBusMessage *m;
   if (!(m = dbus_message_new_method_call("org.freedesktop.login1", logind.spath.data, "org.freedesktop.login1.Session", "ReleaseControl")))
      return;

   dbus_connection_send(logind.dbus, m, NULL);
   dbus_message_unref(m);
}

static bool
get_vt(const char *sid, int *out)
{
   return (sd_session_get_vt(sid, (unsigned*)out) >= 0);
}

bool
wlc_logind_available(void)
{
   char *sid;
   if (sd_pid_get_session(getpid(), &sid) < 0)
      return false;

   char *seat;
   if (!sd_session_is_active(sid) || sd_session_get_seat(sid, &seat) < 0)
      goto error0;

   int vt;
   if (!get_vt(sid, &vt))
      goto error1;

   free(sid);
   free(seat);
   return true;

error1:
   free(seat);
error0:
   free(sid);
   return false;
}

void
wlc_logind_terminate(void)
{
   if (logind.pending.active) {
      dbus_pending_call_cancel(logind.pending.active);
      dbus_pending_call_unref(logind.pending.active);
   }

   release_control();
   free(logind.sid);
   free(logind.seat);
   chck_string_release(&logind.spath);
   wlc_dbus_close(logind.dbus, logind.dbus_ctx);

   memset(&logind, 0, sizeof(logind));
}

int
wlc_logind_init(const char *seat_id)
{
   assert(seat_id);

   if (logind.vt != 0)
      return logind.vt;

   if (sd_pid_get_session(getpid(), &logind.sid) < 0)
      goto session_fail;

   if (sd_session_get_seat(logind.sid, &logind.seat) < 0)
      goto seat_fail;

   if (!chck_cstreq(seat_id, logind.seat))
      goto seat_does_not_match;

   if (!get_vt(logind.sid, &logind.vt))
      goto not_a_vt;

   if (!wlc_dbus_open(wlc_event_loop(), DBUS_BUS_SYSTEM, &logind.dbus, &logind.dbus_ctx) || !setup_dbus() || !take_control())
      goto dbus_fail;

   wlc_log(WLC_LOG_INFO, "logind: session control granted");
   return logind.vt;

session_fail:
   wlc_log(WLC_LOG_WARN, "logind: not running in a systemd session");
   goto fail;
seat_fail:
   wlc_log(WLC_LOG_ERROR, "logind: failed to get session seat");
   goto fail;
seat_does_not_match:
   wlc_log(WLC_LOG_ERROR, "logind: seat does not match wlc seat (%s != %s)", seat_id, logind.seat);
   goto fail;
not_a_vt:
   wlc_log(WLC_LOG_ERROR, "logind: session not running on a VT");
   goto fail;
dbus_fail:
   wlc_log(WLC_LOG_ERROR, "logind: failed to setup dbus");
fail:
   wlc_logind_terminate();
   return 0;
}
