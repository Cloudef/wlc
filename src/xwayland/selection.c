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
   xcb_atom_t target;

   if (strcmp(type, "text/plain;charset=utf-8") == 0) {
      target = xwm->atoms[UTF8_STRING];
   } else if (strcmp(type, "text/plain") == 0) {
      target = xwm->atoms[TEXT];
   } else {
      wlc_log(WLC_LOG_WARN, "Unsupported data source mime type given: %s", type);
      close(fd);
      return;
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
   while (value < end) {
      if (*value == xwm->atoms[UTF8_STRING]) {
         destination = chck_iter_pool_push_back(&xwm->selection.data_source.types, NULL);
         chck_string_set_cstr(destination, "text/plain;charset=utf-8", true);
      }

      if (*value == xwm->atoms[TEXT]) {
         destination = chck_iter_pool_push_back(&xwm->selection.data_source.types, NULL);
         chck_string_set_cstr(destination, "text/plain", true);
      }

      ++value;
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

   XCB_CALL(xwm, xcb_change_property_checked(xwm->connection, XCB_PROP_MODE_REPLACE, xwm->selection.data_requestor, xwm->selection.data_requestor_property, xwm->selection.data_requestor_target, 8, size, array.data));
   send_selection_notify(xwm, xwm->selection.data_requestor, xwm->selection.data_requestor_property, xwm->selection.data_requestor_target);
   wlc_dlog(WLC_DBG_XWM, "Successfully sent data\n");
   goto cleanup;

fail:
   send_selection_notify(xwm, xwm->selection.data_requestor, XCB_ATOM_NONE, xwm->selection.data_requestor_target);

cleanup:
   wl_event_source_remove(xwm->selection.data_event_source);
   xwm->selection.data_event_source = NULL;
   xwm->selection.data_requestor_property = 0;
   xwm->selection.data_requestor = 0;
   return size;
}

static void send_selection_targets(struct wlc_xwm *xwm, xcb_window_t requestor, xcb_atom_t property)
{
   if (!xwm->seat->manager.source) {
      wlc_log(WLC_LOG_WARN, "cannot send selection targets, no source");
      return;
   }

   size_t targets_count = 1;
   xcb_atom_t targets[] = {
      xwm->atoms[TARGETS],
      xwm->atoms[UTF8_STRING],
      xwm->atoms[TEXT]
   };

   struct chck_string *type;
   chck_iter_pool_for_each(&xwm->seat->manager.source->types, type) {
      if (strcmp(type->data, "text/plain") == 0) {
         targets_count = 3;
         xwm->selection.send_type = "text/plain";
         continue;
      }

      if (strcmp(type->data, "text/plain;charset=utf-8") == 0) {
         targets_count = 3;
         xwm->selection.send_type = "text/plain;charset=utf-8";
         break;
      }
   }

   XCB_CALL(xwm, xcb_change_property_checked(xwm->connection, XCB_PROP_MODE_REPLACE, requestor, property, XCB_ATOM_ATOM, 32, targets_count, targets));
   send_selection_notify(xwm, requestor, property, xwm->atoms[TARGETS]);
}

static void send_selection_data(struct wlc_xwm *xwm, xcb_window_t requestor, xcb_atom_t property, xcb_atom_t target)
{
   if (!xwm->seat->manager.source) {
      wlc_log(WLC_LOG_WARN, "cannot send selection data, no source");
      return;
   }

   int pipes[2];
   if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK) == -1) {
      wlc_log(WLC_LOG_WARN, "pipe2 failed: %d", errno);
      send_selection_notify(xwm, requestor, XCB_ATOM_NONE, target);
      return;
   }

   xwm->selection.recv_fd = pipes[0];
   xwm->selection.data_requestor = requestor;
   xwm->selection.data_requestor_property = property;
   xwm->selection.data_requestor_target = target;
   xwm->seat->manager.source->impl->send(xwm->seat->manager.source, xwm->selection.send_type, pipes[1]);
   close(pipes[1]);

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
   } else if (request->target == xwm->atoms[UTF8_STRING] || request->target == xwm->atoms[TEXT]) {
      send_selection_data(xwm, request->requestor, request->property, request->target);
   } else {
      wlc_log(WLC_LOG_WARN, "selection request with invalid target");
      send_selection_notify(xwm, request->requestor, XCB_ATOM_NONE, request->target);
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
   wl_list_remove(&xwm->selection.listener.link);
   wlc_data_source_release(&xwm->selection.data_source);
}

bool wlc_xwm_selection_init(struct wlc_xwm *xwm)
{
   xcb_xfixes_query_version_reply_t *xfixes_reply = NULL;
   memset(&xwm->selection, 0, sizeof(xwm->selection));
   xwm->selection.send_fd = xwm->selection.recv_fd = -1;

   if (!(wlc_data_source(&xwm->selection.data_source, &data_source_impl)))
      goto fail;

   wl_signal_add(&wlc_system_signals()->selection, &xwm->selection.listener);
   xwm->selection.listener.notify = selection_changed;

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
