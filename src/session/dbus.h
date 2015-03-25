#ifndef _WLC_DBUS_H_
#define _WLC_DBUS_H_

#include <dbus/dbus.h>
#include <stdbool.h>
#include "internal.h"

struct wl_event_loop;
struct wl_event_source;

WLC_LOG_ATTR(2, 3) bool wlc_dbus_add_match(DBusConnection *c, const char *format, ...);
bool wlc_dbus_add_match_signal(DBusConnection *c, const char *sender, const char *iface, const char *member, const char *path);
WLC_LOG_ATTR(2, 3) void wlc_dbus_remove_match(DBusConnection *c, const char *format, ...);
void wlc_dbus_remove_match_signal(DBusConnection *c, const char *sender, const char *iface, const char *member, const char *path);

void wlc_dbus_close(DBusConnection *c, struct wl_event_source *ctx);
bool wlc_dbus_open(struct wl_event_loop *loop, DBusBusType bus, DBusConnection **out, struct wl_event_source **ctx_out);

#endif /* _WLC_DBUS_H_ */
