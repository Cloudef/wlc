#include <xcb/xcbext.h>
#include <xcb/xfixes.h>
#include "resources/types/data-source.h"
#include "compositor/seat/data.h"
#include "compositor/seat/seat.h"
#include "internal.h"
#include "xwm.h"
#include "xutil.h"

static void data_source_accept(struct wlc_data_source *source, const char *type)
{
}

static void data_source_cancel(struct wlc_data_source *source)
{
}

static void data_source_send(struct wlc_data_source *source, const char *type, int fd)
{
   struct wlc_xwm *xwm = wl_container_of(source, xwm, selection.data_source);

   if (strcmp(type, "text/plain;charset=utf-8") == 0) {
      xcb_convert_selection(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD], xwm->atoms[UTF8_STRING], xwm->atoms[UTF8_STRING], XCB_TIME_CURRENT_TIME);
      xcb_flush(xwm->connection);

		// fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
      xwm->selection.fd = fd;
   }
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
   if (source == NULL && xwm->selection.clipboard_owner != XCB_WINDOW_NONE) {
      xcb_set_selection_owner(xwm->connection, XCB_WINDOW_NONE, xwm->atoms[CLIPBOARD], XCB_TIME_CURRENT_TIME);
   } else if(source->impl != &data_source_impl) {
      xcb_set_selection_owner(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD], XCB_CURRENT_TIME);
   }
}

static void handle_xfixes_selection_notify(struct wlc_xwm *xwm, xcb_generic_event_t *event) {
   wlc_dlog(WLC_DBG_XWM, "XCB_XFIXES_SELECTION_NOTIFY");
   xcb_xfixes_selection_notify_event_t *notify = (xcb_xfixes_selection_notify_event_t*) event;

   if (notify->selection != xwm->atoms[CLIPBOARD])
      return;

   xwm->selection.clipboard_owner = notify->owner;
   if (notify->owner == XCB_WINDOW_NONE && notify->owner != xwm->window) {
      xwm->seat->manager.source = NULL;
      wl_signal_emit(&wlc_system_signals()->selection, NULL);
      return;
   }

   xcb_convert_selection(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD], xwm->atoms[TARGETS], xwm->atoms[TARGETS], notify->timestamp);
}

static void get_selection_targets(struct wlc_xwm *xwm) {

}

static void get_selection_data(struct wlc_xwm *xwm) {
   
}

static void handle_selection_notify(struct wlc_xwm *xwm, xcb_generic_event_t *event) {
   wlc_dlog(WLC_DBG_XWM, "XCB_SELECTION_NOTIFY");

   xcb_selection_notify_event_t *selection_notify =
   (xcb_selection_notify_event_t *) event;

   if (selection_notify->property == XCB_ATOM_NONE) {
      wlc_log(WLC_LOG_INFO, "xselection: selection conversion failed");
   } else if (selection_notify->target == xwm->atoms[TARGETS]) {
      get_selection_targets(xwm);
   } else if (selection_notify->target == xwm->atoms[UTF8_STRING]) {
      get_selection_data(xwm);
   } else {
      wlc_log(WLC_LOG_INFO, "xselection: unknown selection notify target");
   }
}

static void handle_selection_request(struct wlc_xwm *xwm, xcb_generic_event_t *event) {
   wlc_dlog(WLC_DBG_XWM, "XCB_SELECTION_REQUEST");

   // answer the request, i.e. set the property and send selection notify
}

void wlc_xwm_selection_handle_property_notify(struct wlc_xwm *xwm, xcb_property_notify_event_t *event) {
   // only needed for incr/large data chunks
}

bool wlc_xwm_selection_handle_event(struct wlc_xwm *xwm, xcb_generic_event_t *event) {
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
      break;
   case XCB_SELECTION_REQUEST:
      handle_selection_request(xwm, event);
      break;
   default:
      return false;
   }

   return true;
}

void wlc_xwm_selection_release(struct wlc_xwm *xwm)
{
   wl_list_remove(&xwm->selection.listener.link);
}

bool wlc_xwm_selection_init(struct wlc_xwm *xwm)
{
   xwm->selection.fd = -1;
   xwm->selection.clipboard_owner = 0;

   wl_signal_add(&wlc_system_signals()->selection, &xwm->selection.listener);
   xwm->selection.listener.notify = selection_changed;

   xcb_xfixes_query_version_reply_t *xfixes_reply = NULL;
   if (!(xwm->selection.xfixes = xcb_get_extension_data(xwm->connection, &xcb_xfixes_id)) || !xwm->selection.xfixes->present)
      goto xfixes_fail;

   if (!(xfixes_reply = xcb_xfixes_query_version_reply(xwm->connection, xcb_xfixes_query_version(xwm->connection, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION), NULL)))
      goto xfixes_fail;

   wlc_log(WLC_LOG_INFO, "xfixes (%d.%d)", xfixes_reply->major_version, xfixes_reply->minor_version);
   free(xfixes_reply);

   XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD_MANAGER], XCB_CURRENT_TIME));
   uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
   XCB_CALL(xwm, xcb_xfixes_select_selection_input_checked(xwm->connection, xwm->window, xwm->atoms[CLIPBOARD], mask));
   return true;

xfixes_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get xfixes extension");
   free(xfixes_reply);
   wlc_xwm_selection_release(xwm);
   return false;
}
