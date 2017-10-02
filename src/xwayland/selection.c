#include <xcb/xcbext.h>
#include <xcb/xfixes.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <chck/string/string.h>
#include "resources/types/data-source.h"
#include "compositor/seat/data.h"
#include "compositor/seat/seat.h"
#include "internal.h"
#include "xwm.h"
#include "xutil.h"

// Tried conversions map, ordered by importance
static struct conversion_candidate {
   bool x11_to_wl; // whether to use it when converting from atom name to mime_type
   bool wl_to_x11; // whether to use it when converting from mime_type to atom name
   unsigned int atom_name; // the atom name from the atom_name enum (xutil.h)
   const char *mime_type;
   bool used; // temporary custom data
} conversions_map[] = {
   {true, true, TEXT, "text/plain", false},
   {true, true, UTF8_STRING, "text/plain;charset=utf-8", false},
   {true, true, STRING, "text/plain;charset=iso-8859-1", false},
   {false, true, STRING, "text/plain", false},
   {false, true, TEXT, "text/plain;charset=iso-8859-1", false},
   {true, false, UTF8_STRING, "text/plain", false},
   {false, true, TEXT, "text/plain;charset=utf-8", false}
};

static const char *name_for_atom(struct wlc_xwm *xwm, xcb_atom_t atom)
{
   static char name[256];

   xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(xwm->connection, atom);
   xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(xwm->connection, cookie, NULL);

   if (!reply) {
      wlc_log(WLC_LOG_WARN, "Failed to retrieve atom name of %d", atom);
      name[0] = '\0';
      return name;
   }

   snprintf(name, sizeof name, "%.*s", xcb_get_atom_name_name_length(reply), xcb_get_atom_name_name(reply));
   free(reply);

   return name;
}

static xcb_atom_t atom_for_name(struct wlc_xwm *xwm, const char *name)
{
   xcb_intern_atom_cookie_t cookie = xcb_intern_atom(xwm->connection, 0, strlen(name), name);
   xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(xwm->connection, cookie, NULL);

   if (!reply) {
      wlc_log(WLC_LOG_WARN, "Failed to retrieve atom %s", name);
      return XCB_ATOM_NONE;
   }

   xcb_atom_t atom = reply->atom;
   free(reply);
   return atom;
}

static void send_selection_notify(struct wlc_xwm *xwm, xcb_window_t requestor, xcb_atom_t property, xcb_atom_t target)
{
   xcb_selection_notify_event_t notify;
   memset(&notify, 0, sizeof(notify));

   notify.response_type = XCB_SELECTION_NOTIFY;
   notify.sequence = 0;
   notify.time = XCB_CURRENT_TIME;
   notify.requestor = requestor;
   notify.selection = xwm->atoms[CLIPBOARD];
   notify.target = target;
   notify.property = property;

   xcb_send_event(xwm->connection, 0, requestor, XCB_EVENT_MASK_NO_EVENT, (const char*) &notify);
   xcb_flush(xwm->connection);
}

static void data_source_accept(struct wlc_data_source *source, const char *type)
{
   ((void)source);
   ((void)type);
}

static void data_source_cancel(struct wlc_data_source *source)
{
   ((void)source);
}

static void data_source_send(struct wlc_data_source *source, const char *type, int fd)
{
   struct wlc_xwm *xwm = wl_container_of(source, xwm, selection.data_source);

   xcb_atom_t target = XCB_ATOM_NONE;
   for (unsigned int i = 0; i < sizeof(conversions_map) / sizeof(conversions_map[0]); ++i) {
      struct conversion_candidate *entry = &conversions_map[i];
      if (entry->x11_to_wl && strcmp(type, entry->mime_type) && entry->used) {
         target = xwm->atoms[entry->atom_name];
         break;
      }
   }

   if (target == XCB_ATOM_NONE) {
      target = atom_for_name(xwm, type);
      if (target == XCB_ATOM_NONE) {
         wlc_log(WLC_LOG_WARN, "cannot send selection data, invalid mime type '%s' requested", type);
         close(fd);
         return;
      }
   }

   // request the data in the clipboard property of our window
   XCB_CALL(xwm, xcb_convert_selection_checked(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD], target, xwm->atoms[WLC_SELECTION], XCB_TIME_CURRENT_TIME));
   xcb_flush(xwm->connection);

   if (xwm->selection.send_fd != -1)
      close(xwm->selection.send_fd);

   fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
   xwm->selection.send_fd = fd;
}

static const struct wlc_data_source_impl data_source_impl = {
   .accept = data_source_accept,
   .send = data_source_send,
   .cancel = data_source_cancel
};

static void selection_changed(struct wl_listener *listener, void *data)
{
   struct wlc_data_source *source = data;
   struct wlc_xwm *xwm = wl_container_of(listener, xwm, selection.listener);
   if (source == NULL && xwm->selection.clipboard_owner == xwm->window) {
      XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->connection, XCB_WINDOW_NONE, xwm->atoms[CLIPBOARD], XCB_TIME_CURRENT_TIME));
      xwm->selection.clipboard_owner = 0;
   } else if (source && source->impl != &data_source_impl) {
      XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD], XCB_CURRENT_TIME));
   }
}

static void handle_xfixes_selection_notify(struct wlc_xwm *xwm, xcb_generic_event_t *event)
{
   wlc_dlog(WLC_DBG_XWM, "XCB_XFIXES_SELECTION_NOTIFY");
   xcb_xfixes_selection_notify_event_t *notify = (xcb_xfixes_selection_notify_event_t*) event;

   if (notify->selection != xwm->atoms[CLIPBOARD] || notify->owner == xwm->window)
      return;

   xwm->selection.clipboard_owner = notify->owner;
   if (notify->owner == XCB_WINDOW_NONE && xwm->selection.clipboard_owner != xwm->window) {
      wlc_data_device_manager_set_source(&xwm->seat->manager, NULL);
      return;
   }

   XCB_CALL(xwm, xcb_convert_selection_checked(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD], xwm->atoms[TARGETS], xwm->atoms[WLC_SELECTION], notify->timestamp));
}

static void get_selection_targets(struct wlc_xwm *xwm)
{
   xcb_get_property_cookie_t cookie = xcb_get_property(xwm->connection, 1, xwm->window, xwm->atoms[WLC_SELECTION], XCB_GET_PROPERTY_TYPE_ANY, 0, 1024);
   xcb_get_property_reply_t *reply = xcb_get_property_reply(xwm->connection, cookie, NULL);
   if (reply == NULL) {
      wlc_dlog(WLC_DBG_XWM, "Failed to retrieve xselecion targets property");
      return;
   }

   if (reply->type != XCB_ATOM_ATOM) {
      wlc_dlog(WLC_DBG_XWM, "Invalid xselection targets property type");
      free(reply);
      return;
   }

   // we set a new data source, clean up everything from old sources
   xwm->selection.send_type = "text/plain;charset=utf-8";
   if (xwm->selection.send_fd != -1) {
      close(xwm->selection.send_fd);
      xwm->selection.send_fd = -1;
   }

   if (xwm->selection.recv_fd != -1) {
      close(xwm->selection.recv_fd);
      xwm->selection.recv_fd = -1;
   }

   if (xwm->selection.data_event_source) {
      wl_event_source_remove(xwm->selection.data_event_source);
      xwm->selection.data_event_source = NULL;
   }

   wlc_data_device_manager_set_source(&xwm->seat->manager, &xwm->selection.data_source);

   xcb_atom_t *value = xcb_get_property_value(reply);
   xcb_atom_t *end = value + (xcb_get_property_value_length(reply) / sizeof(xcb_atom_t));
   struct chck_string *destination;

   bool first = false;
   for (; value < end; ++value) {
      bool found = false;
      for (unsigned int i = 0; i < sizeof(conversions_map) / sizeof(conversions_map[0]); ++i) {
         struct conversion_candidate *entry = &conversions_map[i];
         if (entry->x11_to_wl && xwm->atoms[entry->atom_name] == *value) {
            destination = chck_iter_pool_push_back(&xwm->selection.data_source.types, NULL);
            chck_string_set_cstr(destination, entry->mime_type, true);
            found = true;
            entry->used = true;

            if (!first)
               break;
         } else if (first) {
            entry->used = false;
         }
      }

      first = false;
      if (!found) {
         const char *name = name_for_atom(xwm, *value);
         if (name[0] == '\0') {
            wlc_log(WLC_LOG_WARN, "received invalid supported target atom");
            continue;
         }

         // check if could be a mime-type
         if (strchr(name, '/') == NULL)
            continue;

         destination = chck_iter_pool_push_back(&xwm->selection.data_source.types, NULL);
         chck_string_set_cstr(destination, name, true);
      }
   }

   free(reply);
}

static void get_selection_data(struct wlc_xwm *xwm)
{
   if (xwm->selection.send_fd == -1)
      return;

   xcb_get_property_cookie_t cookie = xcb_get_property(xwm->connection, 1, xwm->window, xwm->atoms[WLC_SELECTION], XCB_GET_PROPERTY_TYPE_ANY, 0, 65536);
   xcb_get_property_reply_t *reply = xcb_get_property_reply(xwm->connection, cookie, NULL);

   if (reply == NULL) {
      wlc_log(WLC_LOG_WARN, "xselection: failed to retrieve selection data");
      return;
   }

   write(xwm->selection.send_fd, xcb_get_property_value(reply), xcb_get_property_value_length(reply));
   close(xwm->selection.send_fd);
   xwm->selection.send_fd = -1;
}

static void handle_selection_notify(struct wlc_xwm *xwm, xcb_generic_event_t *event)
{
   wlc_dlog(WLC_DBG_XWM, "XCB_SELECTION_NOTIFY");

   xcb_selection_notify_event_t *notify = (xcb_selection_notify_event_t *) event;

   if (notify->property == XCB_ATOM_NONE) {
      wlc_log(WLC_LOG_INFO, "xselection: selection conversion failed");
   } else if (notify->target == xwm->atoms[TARGETS]) {
      get_selection_targets(xwm);
   } else if (notify->target == xwm->atoms[UTF8_STRING] || notify->target == xwm->atoms[TEXT]) {
      get_selection_data(xwm);
   } else {
      wlc_log(WLC_LOG_INFO, "xselection: unknown selection notify target");
   }
}

static int recv_data_source(int fd, uint32_t mask, void *data)
{
   ((void)mask);
   struct wlc_xwm *xwm = data;

   const int readCount = 4096;
   struct wl_array array;
   wl_array_init(&array);

   int ret = readCount;
   int size = 0;
   while (ret == readCount) {
      void *ptr = wl_array_add(&array, readCount);
      ret = read(fd, ptr, readCount);
      if (ret < 0) {
         wlc_log(WLC_LOG_ERROR, "failed to read data source fd: %d", errno);
         goto fail;
      }

      size += ret;
   }

   XCB_CALL(xwm, xcb_change_property_checked(xwm->connection, XCB_PROP_MODE_REPLACE, xwm->selection.data_requestor, xwm->selection.data_request_property, xwm->selection.data_request_target, 8, size, array.data));
   send_selection_notify(xwm, xwm->selection.data_requestor, xwm->selection.data_request_property, xwm->selection.data_request_target);
   wlc_dlog(WLC_DBG_XWM, "Successfully sent data\n");
   goto cleanup;

fail:
   send_selection_notify(xwm, xwm->selection.data_requestor, XCB_ATOM_NONE, xwm->selection.data_request_target);

cleanup:
   wl_event_source_remove(xwm->selection.data_event_source);
   xwm->selection.data_event_source = NULL;
   xwm->selection.data_request_property = 0;
   xwm->selection.data_request_target = 0;
   xwm->selection.data_requestor = 0;
   return size;
}

static void send_selection_targets(struct wlc_xwm *xwm, xcb_window_t requestor, xcb_atom_t property)
{
   if (!xwm->seat->manager.source) {
      wlc_log(WLC_LOG_WARN, "cannot send selection targets, no source");
      return;
   }

   struct wl_array targets;
   wl_array_init(&targets);
   *((xcb_atom_t*) wl_array_add(&targets, sizeof(xcb_atom_t))) = xwm->atoms[TARGETS];

   struct chck_string *type;
   bool first = true;
   chck_iter_pool_for_each(&xwm->seat->manager.source->types, type) {
      bool found = false;
      for (unsigned int i = 0; i < sizeof(conversions_map) / sizeof(conversions_map[0]); ++i) {
         struct conversion_candidate *entry = &conversions_map[i];
         if (entry->wl_to_x11 && strcmp(type->data, entry->mime_type) == 0) {
            *((xcb_atom_t*) wl_array_add(&targets, sizeof(xcb_atom_t))) = xwm->atoms[entry->atom_name];
            found = true;
            entry->used = true;

            if (!first)
               break;
         } else if (first) {
            entry->used = false;
         }
      }

      first = false;
      if (!found) {
         xcb_atom_t atom = atom_for_name(xwm, type->data);
         if (atom != XCB_ATOM_NONE)
            *((xcb_atom_t*) wl_array_add(&targets, sizeof(xcb_atom_t))) = atom;
      }
   }

   int length = targets.size / sizeof(xcb_atom_t);
   XCB_CALL(xwm, xcb_change_property_checked(xwm->connection, XCB_PROP_MODE_REPLACE, requestor, property, XCB_ATOM_ATOM, 32, length, targets.data));
   send_selection_notify(xwm, requestor, property, xwm->atoms[TARGETS]);
}

static void send_selection_data(struct wlc_xwm *xwm, xcb_window_t requestor, xcb_atom_t property, xcb_atom_t target)
{
   if (!xwm->seat->manager.source) {
      wlc_log(WLC_LOG_WARN, "cannot send selection data, no source");
      send_selection_notify(xwm, requestor, XCB_ATOM_NONE, target);
      return;
   }

   int pipes[2];
   if (pipe(pipes) == -1) {
      wlc_log(WLC_LOG_WARN, "pipe failed: %d", errno);
      send_selection_notify(xwm, requestor, XCB_ATOM_NONE, target);
      return;
   }

   fcntl(pipes[0], F_SETFD, O_CLOEXEC | O_NONBLOCK);
   fcntl(pipes[1], F_SETFD, O_CLOEXEC | O_NONBLOCK);
   xwm->selection.send_type = NULL;
   for (unsigned int i = 0; i < sizeof(conversions_map) / sizeof(conversions_map[0]); ++i) {
      struct conversion_candidate *entry = &conversions_map[i];
      if (entry->wl_to_x11 && target == xwm->atoms[entry->atom_name] && entry->used) {
         xwm->selection.send_type = entry->mime_type;
         break;
      }
   }

   if (!xwm->selection.send_type) {
      const char *name = name_for_atom(xwm, target);
      if (name[0] == '\0' || strchr(name, '/') == NULL) {
         wlc_log(WLC_LOG_WARN, "cannot send selection data, invalid target atom");
         send_selection_notify(xwm, requestor, XCB_ATOM_NONE, target);
         return;
      }

      xwm->selection.send_type = name;
   }

   xwm->selection.recv_fd = pipes[0];
   xwm->selection.data_requestor = requestor;
   xwm->selection.data_request_property = property;
   xwm->selection.data_request_target = target;
   xwm->seat->manager.source->impl->send(xwm->seat->manager.source, xwm->selection.send_type, pipes[1]);

   xwm->selection.data_event_source = wl_event_loop_add_fd(wlc_event_loop(), pipes[0], WL_EVENT_READABLE, &recv_data_source, xwm);
}

static void handle_selection_request(struct wlc_xwm *xwm, xcb_generic_event_t *event)
{
   wlc_dlog(WLC_DBG_XWM, "XCB_SELECTION_REQUEST");

   xcb_selection_request_event_t *request = (xcb_selection_request_event_t *) event;

   if (request->selection != xwm->atoms[CLIPBOARD]) {
      wlc_log(WLC_LOG_WARN, "selection request for invalid selection");
      send_selection_notify(xwm, request->requestor, XCB_ATOM_NONE, request->target);
      return;
   }

   if (request->requestor == xwm->window) {
      wlc_log(WLC_LOG_WARN, "received selection request from own window");
      send_selection_notify(xwm, request->requestor, XCB_ATOM_NONE, request->target);
      return;
   }

   if (request->target == xwm->atoms[TARGETS]) {
      send_selection_targets(xwm, request->requestor, request->property);
   } else {
      send_selection_data(xwm, request->requestor, request->property, request->target);
   }
}

bool wlc_xwm_selection_handle_event(struct wlc_xwm *xwm, xcb_generic_event_t *event)
{
   switch (event->response_type - xwm->selection.xfixes->first_event) {
      case XCB_XFIXES_SELECTION_NOTIFY:
         handle_xfixes_selection_notify(xwm, event);
         return true;
      default:
      break;
   }

   switch(event->response_type & ~0x80) {
   case XCB_SELECTION_NOTIFY:
      handle_selection_notify(xwm, event);
      return true;
   case XCB_SELECTION_REQUEST:
      handle_selection_request(xwm, event);
      return true;
   default:
      return false;
   }

   return false;
}

void wlc_xwm_selection_release(struct wlc_xwm *xwm)
{
   if (xwm->selection.send_fd != -1)
      close(xwm->selection.send_fd);

   if (xwm->selection.recv_fd != -1)
      close(xwm->selection.recv_fd);

   if (xwm->selection.data_event_source)
      wl_event_source_remove(xwm->selection.data_event_source);

   wlc_data_source_release(&xwm->selection.data_source);

   if (xwm->selection.listener.notify)
      wl_list_remove(&xwm->selection.listener.link);
}

bool wlc_xwm_selection_init(struct wlc_xwm *xwm)
{
   xcb_xfixes_query_version_reply_t *xfixes_reply = NULL;
   memset(&xwm->selection, 0, sizeof(xwm->selection));
   xwm->selection.send_fd = xwm->selection.recv_fd = -1;

   if (!(wlc_data_source(&xwm->selection.data_source, &data_source_impl)))
      goto fail;

   xwm->selection.listener.notify = selection_changed;
   wl_signal_add(&wlc_system_signals()->selection, &xwm->selection.listener);

   if (!(xwm->selection.xfixes = xcb_get_extension_data(xwm->connection, &xcb_xfixes_id)) || !xwm->selection.xfixes->present)
      goto fail;

   if (!(xfixes_reply = xcb_xfixes_query_version_reply(xwm->connection, xcb_xfixes_query_version(xwm->connection, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION), NULL)))
      goto fail;

   wlc_log(WLC_LOG_INFO, "xfixes (%d.%d)", xfixes_reply->major_version, xfixes_reply->minor_version);
   free(xfixes_reply);

   XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD_MANAGER], XCB_CURRENT_TIME));
   uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
   XCB_CALL(xwm, xcb_xfixes_select_selection_input_checked(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD], mask));

   // manually trigger first data source if there already is any
   if (xwm->seat->manager.source != NULL)
      selection_changed(&xwm->selection.listener, xwm->seat->manager.source);

   return true;

fail:
   wlc_log(WLC_LOG_WARN, "Failed to get xfixes extension");
   free(xfixes_reply);
   wlc_xwm_selection_release(xwm);
   return false;
}
