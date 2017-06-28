#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>
#include <xcb/composite.h>
#include <xcb/xcb_image.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <chck/overflow/overflow.h>
#include "internal.h"
#include "macros.h"
#include "xwm.h"
#include "xwayland.h"
#include "xutil.h"
#include "compositor/compositor.h"
#include "compositor/view.h"
#include "resources/types/surface.h"

bool
xcb_call(struct wlc_xwm *xwm, const char *func, uint32_t line, xcb_void_cookie_t cookie)
{
   xcb_generic_error_t *error;
   if (!(error = xcb_request_check(xwm->connection, cookie)))
      return true;

   wlc_log(WLC_LOG_ERROR, "xwm: function %s at line %u x11 error code %d", func, line, error->error_code);
   free(error);
   return false;
}

static struct wlc_x11_window*
paired_for_id(struct wlc_xwm *xwm, xcb_window_t window)
{
   assert(xwm);

   wlc_handle *h;
   struct wlc_view *view;
   if (!(h = chck_hash_table_get(&xwm->paired, window)) || !(view = convert_from_wlc_handle(*h, "view")))
      return NULL;

   return &view->x11;
}

static struct wlc_x11_window*
unpaired_for_id(struct wlc_xwm *xwm, xcb_window_t window)
{
   assert(xwm);
   return chck_hash_table_get(&xwm->unpaired, window);
}

static void
remove_window_for_id(struct wlc_xwm *xwm, xcb_window_t window)
{
   assert(xwm);

   if (xwm->focus == window)
      xwm->focus = 0;

   struct wlc_x11_window *win;
   if ((win = paired_for_id(xwm, window)))
      memset(win, 0, sizeof(struct wlc_x11_window));

   chck_hash_table_set(&xwm->paired, window, NULL);
   chck_hash_table_set(&xwm->unpaired, window, NULL);
}

static void
remove_window(struct wlc_xwm *xwm, struct wlc_x11_window *win)
{
   assert(xwm && win);
   remove_window_for_id(xwm, win->id);
}

static bool
add_window(struct wlc_xwm *xwm, xcb_window_t window, bool override_redirect)
{
   assert(xwm);
   struct wlc_x11_window win = {0};
   win.id = window;
   win.xwm = xwm;
   win.override_redirect = override_redirect;
   const bool ret = chck_hash_table_set(&xwm->unpaired, window, &win);
   wlc_dlog(WLC_DBG_XWM, "-> Unpaired collisions (%u)", chck_hash_table_collisions(&xwm->unpaired));
   return ret;
}

static struct wlc_view*
view_for_window(struct wlc_x11_window *win)
{
   struct wlc_view *view;
   return (win && win->paired ? wl_container_of(win, view, x11) : NULL);
}

static void
set_parent(struct wlc_xwm *xwm, struct wlc_x11_window *win, xcb_window_t parent_id)
{
   assert(xwm && win);

   if (!parent_id || win->id == parent_id) {
      wlc_view_set_parent_ptr(view_for_window(win), NULL);
      return;
   }

   struct wlc_x11_window *parent;
   if ((parent = paired_for_id(xwm, parent_id)))
      wlc_view_set_parent_ptr(view_for_window(win), view_for_window(parent));
}

enum net_wm_state {
   NET_WM_STATE_REMOVE = 0,
   NET_WM_STATE_ADD    = 1,
   NET_WM_STATE_TOGGLE = 2,
};

static void
handle_state(struct wlc_xwm *xwm, struct wlc_x11_window *win, xcb_atom_t *atoms, size_t nmemb, enum net_wm_state state)
{
   assert(win && atoms);

   struct wlc_view *view;
   if (!(view = view_for_window(win)))
      return;

   for (uint32_t i = 0; i < nmemb; ++i) {
      if (atoms[i] == xwm->atoms[NET_WM_STATE_FULLSCREEN]) {
         bool toggle = !(view->pending.state & WLC_BIT_FULLSCREEN);
         wlc_view_request_state(view, WLC_BIT_FULLSCREEN, (state == 0 ? false : (state == 1 ? true : toggle)));
      } else if (atoms[i] == xwm->atoms[NET_WM_STATE_MODAL] || atoms[i] == xwm->atoms[NET_WM_STATE_ABOVE]) {
         bool toggle = !(view->type & WLC_BIT_MODAL);
         wlc_view_set_type_ptr(view, WLC_BIT_MODAL, (state == 0 ? false : (state == 1 ? true : toggle)));
      }
   }
}

static void
read_properties(struct wlc_xwm *xwm, struct wlc_x11_window *win, const xcb_atom_t *props, size_t nmemb)
{
   assert(win);

   struct wlc_view *view;
   if (!(view = view_for_window(win)))
      return;

   xcb_get_property_cookie_t *cookies;
   if (!(cookies = chck_calloc_of(nmemb, sizeof(xcb_get_property_cookie_t))))
      return;

   for (uint32_t i = 0; i < nmemb; ++i)
      cookies[i] = xcb_get_property(xwm->connection, 0, win->id, props[i], XCB_ATOM_ANY, 0, 2048);

   for (uint32_t i = 0; i < nmemb; ++i) {
      xcb_get_property_reply_t *reply;
      if (!(reply = xcb_get_property_reply(xwm->connection, cookies[i], NULL)))
         continue;

      if (reply->type == XCB_ATOM_STRING || reply->type == xwm->atoms[UTF8_STRING]) {
         // Class && Name
         // STRING == latin1, but we naively just read it as is. For full support we should convert to utf8.
         if (props[i] == XCB_ATOM_WM_CLASS) {
            char *class = xcb_get_property_value(reply);
            size_t class_total_len = xcb_get_property_value_length(reply);

            /* unpack two sequentially stored strings (instance, class) */
            size_t class_instance_len = strnlen(class, class_total_len);

            if (class_instance_len >= class_total_len) {
               /* there doesn't exist a second string in tuple */
               wlc_view_set_instance_ptr(view, class, class_instance_len);
               wlc_view_set_class_ptr(view, class, class_instance_len);
            } else {
               /* different instance and class strings */
               size_t class_class_offset = class_instance_len +1;
               wlc_view_set_instance_ptr(view, class, class_instance_len);
               wlc_view_set_class_ptr(view, class+class_class_offset, class_total_len-class_class_offset);
            }
            wlc_dlog(WLC_DBG_XWM, "WM_INSTANCE: %s", view->data._instance.data);
            wlc_dlog(WLC_DBG_XWM, "WM_CLASS: %s", view->data._class.data);
         } else if (props[i] == XCB_ATOM_WM_NAME || props[i] == xwm->atoms[NET_WM_NAME]) {
            if (reply->type != XCB_ATOM_STRING  || !win->has_utf8_title) {
               wlc_view_set_title_ptr(view, xcb_get_property_value(reply), xcb_get_property_value_length(reply));
               win->has_utf8_title = true;
            }
            wlc_dlog(WLC_DBG_XWM, "(%d) %s %s %s", win->has_utf8_title, (reply->type == XCB_ATOM_STRING ? "STRING" : "UTF8_STRING"), (props[i] == XCB_ATOM_WM_NAME ? "WM_NAME" : "NET_WM_NAME"), view->data.title.data);
         }
      } else if (props[i] == XCB_ATOM_WM_TRANSIENT_FOR && reply->type == XCB_ATOM_WINDOW) {
         // Transient
         xcb_window_t *xid = xcb_get_property_value(reply);
         set_parent(xwm, win, *xid);
         wlc_dlog(WLC_DBG_XWM, "WM_TRANSIENT_FOR: %u", *xid);
      } else if (props[i] == xwm->atoms[NET_WM_PID] && reply->type == XCB_ATOM_CARDINAL) {
         // PID
         wlc_view_set_pid_ptr(view, *(pid_t *)xcb_get_property_value(reply));
         wlc_dlog(WLC_DBG_XWM, "NET_WM_PID");
      } else if (props[i] == xwm->atoms[NET_WM_WINDOW_TYPE] && reply->type == XCB_ATOM_ATOM) {
         // Window type
         view->type &= ~WLC_BIT_UNMANAGED | ~WLC_BIT_SPLASH | ~WLC_BIT_MODAL;
         xcb_atom_t *atoms = xcb_get_property_value(reply);
         for (uint32_t i = 0; i < reply->value_len; ++i) {
            if (atoms[i] == xwm->atoms[NET_WM_WINDOW_TYPE_TOOLTIP] ||
                  atoms[i] == xwm->atoms[NET_WM_WINDOW_TYPE_UTILITY] ||
                  atoms[i] == xwm->atoms[NET_WM_WINDOW_TYPE_DND] ||
                  atoms[i] == xwm->atoms[NET_WM_WINDOW_TYPE_DROPDOWN_MENU] ||
                  atoms[i] == xwm->atoms[NET_WM_WINDOW_TYPE_POPUP_MENU] ||
                  atoms[i] == xwm->atoms[NET_WM_WINDOW_TYPE_COMBO]) {
               wlc_view_set_type_ptr(view, WLC_BIT_UNMANAGED, true);
            }
            if (atoms[i] == xwm->atoms[NET_WM_WINDOW_TYPE_DIALOG])
               wlc_view_set_type_ptr(view, WLC_BIT_MODAL, true);
            if (atoms[i] == xwm->atoms[NET_WM_WINDOW_TYPE_SPLASH])
               wlc_view_set_type_ptr(view, WLC_BIT_SPLASH, true);
         }
         wlc_dlog(WLC_DBG_XWM, "NET_WM_WINDOW_TYPE: %u", view->type);
      } else if (props[i] == xwm->atoms[WM_PROTOCOLS]) {
         xcb_atom_t *atoms = xcb_get_property_value(reply);
         for (uint32_t i = 0; i < reply->value_len; ++i) {
            if (atoms[i] == xwm->atoms[WM_DELETE_WINDOW])
               win->has_delete_window = true;
         }
         wlc_dlog(WLC_DBG_XWM, "WM_PROTOCOLS: %u", view->type);
      } else if (props[i] == xwm->atoms[WM_NORMAL_HINTS]) {
         wlc_dlog(WLC_DBG_XWM, "WM_NORMAL_HINTS");
      } else if (props[i] == xwm->atoms[NET_WM_STATE]) {
         handle_state(xwm, win, xcb_get_property_value(reply), reply->value_len, NET_WM_STATE_ADD);
         wlc_dlog(WLC_DBG_XWM, "NET_WM_STATE");
      } else if (props[i] == xwm->atoms[MOTIF_WM_HINTS]) {
         // Motif hints
         wlc_dlog(WLC_DBG_XWM, "MOTIF_WM_HINTS");
      }

      free(reply);
   }

   free(cookies);
}

static void
get_properties(struct wlc_xwm *xwm, struct wlc_x11_window *win)
{
   const xcb_atom_t props[] = {
      XCB_ATOM_WM_CLASS,
      XCB_ATOM_WM_NAME,
      XCB_ATOM_WM_TRANSIENT_FOR,
      xwm->atoms[WM_PROTOCOLS],
      xwm->atoms[WM_NORMAL_HINTS],
      xwm->atoms[NET_WM_STATE],
      xwm->atoms[NET_WM_WINDOW_TYPE],
      xwm->atoms[NET_WM_NAME],
      xwm->atoms[NET_WM_PID],
      xwm->atoms[MOTIF_WM_HINTS]
   };

   read_properties(xwm, win, props, LENGTH(props));
}

static void
set_geometry(struct wlc_xwm *xwm, xcb_window_t window, const struct wlc_geometry *g)
{
   assert(g);
   const uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH;
   const uint32_t values[] = { g->origin.x, g->origin.y, g->size.w, g->size.h, 0 };
   wlc_dlog(WLC_DBG_XWM, "-> Configure x11 window (%u) %ux%u+%d,%d", window, g->size.w, g->size.h, g->origin.x, g->origin.y);
   XCB_CALL(xwm, xcb_configure_window_checked(xwm->connection, window, mask, (uint32_t*)&values));
   xcb_flush(xwm->connection);
}

static void
get_geometry(struct wlc_xwm *xwm, xcb_window_t window, struct wlc_geometry *out_g, uint32_t *out_depth)
{
   if (out_g)
      *out_g = wlc_geometry_zero;

   if (out_depth)
      *out_depth = 0;

   xcb_get_geometry_reply_t *reply;
   if ((reply = xcb_get_geometry_reply(xwm->connection, xcb_get_geometry(xwm->connection, window), NULL))) {
      if (out_g)
         *out_g = (struct wlc_geometry){ .origin = { reply->x, reply->y }, .size = { reply->width, reply->height } };

      if (out_depth)
         *out_depth = reply->depth;

      free(reply);
   }
}

static void
link_surface(struct wlc_xwm *xwm, struct wlc_x11_window *win, struct wl_resource *resource)
{
   assert(xwm && win);

   struct wlc_compositor *compositor;
   if (!(compositor = wl_container_of(xwm, compositor, xwm)))
      return;

   struct wlc_surface *surface;
   if (!resource || !(surface = convert_from_wl_resource(resource, "surface"))) {
      wlc_dlog(WLC_DBG_XWM, "-> Surface resource for x11 window (%u) does not exist yet", win->id);
      return;
   }

   uint32_t depth;
   struct wlc_geometry geometry;
   get_geometry(xwm, win->id, &geometry, &depth);
   win->has_alpha = (depth == 32);

   // This is not real interactable x11 window most likely, lets just not handle it.
   if (win->override_redirect && geometry.size.w <= 1 && geometry.size.h <= 1) {
      remove_window(xwm, win);
      return;
   }

   struct wlc_view *view;
   if (!(view = wlc_compositor_view_for_surface(compositor, surface))) {
      wlc_x11_window_close(win);
      remove_window(xwm, win);
      return;
   }

   wlc_handle handle = convert_to_wlc_handle(view);
   memcpy(&view->x11, win, sizeof(view->x11));
   chck_hash_table_set(&xwm->paired, win->id, &handle);
   chck_hash_table_set(&xwm->unpaired, win->id, NULL);
   win = NULL; // no longer valid

   wlc_dlog(WLC_DBG_XWM, "-> Paired collisions (%u)", chck_hash_table_collisions(&xwm->paired));

   view->x11.paired = true;
   wlc_view_set_type_ptr(view, WLC_BIT_OVERRIDE_REDIRECT, view->x11.override_redirect);
   get_properties(xwm, &view->x11);

   if (!wlc_geometry_equals(&geometry, &wlc_geometry_zero))
      wlc_view_set_geometry_ptr(view, 0, &geometry);

   wlc_dlog(WLC_DBG_XWM, "-> Linked x11 window (%u) to view (%" PRIuWLC ") [%ux%u+%d,%d]",
            view->x11.id, handle, geometry.size.w, geometry.size.h, geometry.origin.x, geometry.origin.y);

   if (!view->parent && xwm->focus && (view->type & WLC_BIT_MODAL))
      set_parent(xwm, &view->x11, xwm->focus);
}

static void
focus_window(struct wlc_xwm *xwm, xcb_window_t window, bool force)
{
   if (!force && xwm->focus == window)
      return;

   wlc_dlog(WLC_DBG_FOCUS, "-> xwm focus %u", window);

   if (window == 0) {
      XCB_CALL(xwm, xcb_set_input_focus_checked(xwm->connection, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_NONE, XCB_CURRENT_TIME));
      xcb_flush(xwm->connection);
      xwm->focus = 0;
      return;
   }

   xcb_client_message_event_t m = {0};
   m.response_type = XCB_CLIENT_MESSAGE;
   m.format = 32;
   m.window = window;
   m.type = xwm->atoms[WM_PROTOCOLS];
   m.data.data32[0] = xwm->atoms[WM_TAKE_FOCUS];
   m.data.data32[1] = XCB_TIME_CURRENT_TIME;
   XCB_CALL(xwm, xcb_send_event_checked(xwm->connection, 0, window, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char*)&m));
   XCB_CALL(xwm, xcb_set_input_focus_checked(xwm->connection, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME));
   XCB_CALL(xwm, xcb_configure_window_checked(xwm->connection, window, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_ABOVE}));
   xcb_flush(xwm->connection);
   xwm->focus = window;
}

static void
delete_window(struct wlc_xwm *xwm, xcb_window_t window)
{
   xcb_client_message_event_t ev = {0};
   ev.response_type = XCB_CLIENT_MESSAGE;
   ev.window = window;
   ev.format = 32;
   ev.sequence = 0;
   ev.type = xwm->atoms[WM_PROTOCOLS];
   ev.data.data32[0] = xwm->atoms[WM_DELETE_WINDOW];
   ev.data.data32[1] = XCB_CURRENT_TIME;
   XCB_CALL(xwm, xcb_send_event_checked(xwm->connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (char*)&ev));
}

static WLC_PURE enum wlc_surface_format
wlc_x11_window_get_surface_format(struct wlc_x11_window *win)
{
   assert(win);
   return (win->has_alpha ? SURFACE_RGBA : SURFACE_RGB);
}

void
wlc_x11_window_set_surface_format(struct wlc_surface *surface, struct wlc_x11_window *win)
{
   surface->format = wlc_x11_window_get_surface_format(win);
}

void
wlc_x11_window_close(struct wlc_x11_window *win)
{
   assert(win);

   if (!win->xwm->connection || !win->id)
      return;

   if (win->has_delete_window) {
      delete_window(win->xwm, win->id);
   } else {
      XCB_CALL(win->xwm, xcb_kill_client_checked(win->xwm->connection, win->id));
   }

   xcb_flush(win->xwm->connection);
}

void
wlc_x11_window_configure(struct wlc_x11_window *win, const struct wlc_geometry *g)
{
   assert(win && g);

   if (!win->xwm->connection || !win->id)
      return;

   set_geometry(win->xwm, win->id, g);
}

void
wlc_x11_window_set_state(struct wlc_x11_window *win, enum wlc_view_state_bit state, bool toggle)
{
   assert(win);

   if (!win->xwm->connection || !win->id)
      return;

   if (state == WLC_BIT_FULLSCREEN)
      XCB_CALL(win->xwm, xcb_change_property_checked(win->xwm->connection, XCB_PROP_MODE_REPLACE, win->id, win->xwm->atoms[NET_WM_STATE], XCB_ATOM_ATOM, 32, (toggle ? 1 : 0), (toggle ? &win->xwm->atoms[NET_WM_STATE_FULLSCREEN] : NULL)));
}

bool
wlc_x11_window_set_active(struct wlc_x11_window *win, bool active)
{
   assert(win);

   if (!win->xwm || !win->xwm->connection || !win->id || win->override_redirect)
      return false;

   if (active) {
      focus_window(win->xwm, win->id, false);
   } else if (win->id == win->xwm->focus) {
      focus_window(win->xwm, 0, false);
   }

   return true;
}

static void
handle_client_message(struct wlc_xwm *xwm, xcb_client_message_event_t *ev)
{
   assert(ev);

   if (ev->type == xwm->atoms[WL_SURFACE_ID]) {
      struct wlc_x11_window *win;
      if (!(win = unpaired_for_id(xwm, ev->window)))
         return;

      win->surface_id = ev->data.data32[0];
      link_surface(xwm, win, wl_client_get_object(wlc_xwayland_get_client(), ev->data.data32[0]));
      return;
   }

   struct wlc_x11_window *win;
   if (!(win = paired_for_id(xwm, ev->window)))
      return;

   if (ev->type == xwm->atoms[NET_WM_STATE])
      handle_state(xwm, win, &ev->data.data32[1], 2, ev->data.data32[0]);
}

static int
x11_event(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;
   struct wlc_xwm *xwm = data;

   int count = 0;
   xcb_generic_event_t *event;
   while ((event = xcb_poll_for_event(xwm->connection))) {
      switch (event->response_type & ~0x80) {
         case 0:
            wlc_log(WLC_LOG_ERROR, "xwm: Uncaught X11 error occured");
            break;

         case XCB_CREATE_NOTIFY:
         {
            xcb_create_notify_event_t *ev = (xcb_create_notify_event_t*)event;
            wlc_dlog(WLC_DBG_XWM, "XCB_CREATE_NOTIFY (%u : %d)", ev->window, ev->override_redirect);
            add_window(xwm, ev->window, ev->override_redirect);
         }
         break;

         case XCB_MAP_NOTIFY:
         {
            xcb_map_notify_event_t *ev = (xcb_map_notify_event_t*)event;
            wlc_dlog(WLC_DBG_XWM, "XCB_MAP_NOTIFY (%u)", ev->window);

            struct wlc_x11_window *win;
            if ((win = paired_for_id(xwm, ev->window)) || (win = unpaired_for_id(xwm, ev->window))) {
               win->override_redirect = ev->override_redirect;
            } else {
               add_window(xwm, ev->window, ev->override_redirect);
            }
         }
         break;

         case XCB_UNMAP_NOTIFY:
         {
            xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t*)event;
            wlc_dlog(WLC_DBG_XWM, "XCB_UNMAP_NOTIFY (%u)", ev->window);
            remove_window_for_id(xwm, ev->window);
         }
         break;

         case XCB_DESTROY_NOTIFY:
         {
            xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t*)event;
            wlc_dlog(WLC_DBG_XWM, "XCB_DESTROY_NOTIFY (%u)", ev->window);
            remove_window_for_id(xwm, ev->window);
         }
         break;

         case XCB_MAP_REQUEST:
         {
            xcb_map_request_event_t *ev = (xcb_map_request_event_t*)event;
            wlc_dlog(WLC_DBG_XWM, "XCB_MAP_REQUEST (%u)", ev->window);
            XCB_CALL(xwm, xcb_change_window_attributes_checked(xwm->connection, ev->window, XCB_CW_EVENT_MASK, &(uint32_t){XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE}));
            XCB_CALL(xwm, xcb_map_window_checked(xwm->connection, ev->window));
         }
         break;

         case XCB_CLIENT_MESSAGE:
            wlc_dlog(WLC_DBG_XWM, "XCB_CLIENT_MESSAGE");
            handle_client_message(xwm, (xcb_client_message_event_t*)event);
            break;

         case XCB_FOCUS_IN:
         {
            // Do not let clients to steal focus
            xcb_focus_in_event_t *ev = (xcb_focus_in_event_t*)event;
            wlc_dlog(WLC_DBG_XWM, "XCB_FOCUS_IN (%u) [%u]", ev->event, xwm->focus);
            if (xwm->focus && xwm->focus != ev->event)
               focus_window(xwm, xwm->focus, true);
         }
         break;

         case XCB_PROPERTY_NOTIFY:
         {
            xcb_property_notify_event_t *ev = (xcb_property_notify_event_t*)event;
            wlc_dlog(WLC_DBG_XWM, "XCB_PROPERTY_NOTIFY (%u)", ev->window);
            struct wlc_x11_window *win;
            if ((win = paired_for_id(xwm, ev->window)) || (win = unpaired_for_id(xwm, ev->window)))
               read_properties(xwm, win, &ev->atom, 1);

            wlc_xwm_selection_handle_property_notify(xwm, ev);
         }
         break;

         case XCB_CONFIGURE_REQUEST:
         {
            xcb_configure_request_event_t *ev = (xcb_configure_request_event_t*)event;
            wlc_dlog(WLC_DBG_XWM, "XCB_CONFIGURE_REQUEST (%u) [%ux%u+%d,%d]", ev->window, ev->width, ev->height, ev->x, ev->y);

            // Some windows freeze unless they get what they want.
            const struct wlc_geometry r = { { ev->x, ev->y }, { ev->width, ev->height } };
            set_geometry(xwm, ev->window, &r);

            struct wlc_view *view;
            struct wlc_x11_window *win;
            if ((win = paired_for_id(xwm, ev->window)) && (view = view_for_window(win))) {
               set_parent(xwm, win, ev->parent);
               wlc_view_request_geometry(view, &r);
            }
         }
         break;

         case XCB_CONFIGURE_NOTIFY:
         {
            xcb_configure_notify_event_t *ev = (xcb_configure_notify_event_t*)event;
            wlc_dlog(WLC_DBG_XWM, "XCB_CONFIGURE_NOTIFY (%u)", ev->window);
            struct wlc_view *view;
            struct wlc_x11_window *win;
            if ((win = paired_for_id(xwm, ev->window)) || (win = unpaired_for_id(xwm, ev->window))) {
               win->override_redirect = ev->override_redirect;

               if ((view = view_for_window(win)))
                  wlc_view_set_type_ptr(view, WLC_BIT_OVERRIDE_REDIRECT, win->override_redirect);
            }
         }
         break;

         // TODO: Handle?
         case XCB_FOCUS_OUT:
            wlc_dlog(WLC_DBG_XWM, "XCB_FOCUS_OUT");
            break;
         case XCB_MAPPING_NOTIFY:
            wlc_dlog(WLC_DBG_XWM, "XCB_MAPPING_NOTIFY");
            break;

         default:
            if (!wlc_xwm_selection_handle_event(xwm, event))
               wlc_log(WLC_LOG_WARN, "xwm: unimplemented event %d", event->response_type & ~0x80);

            break;
      }

      free(event);
      count += 1;
   }

   xcb_flush(xwm->connection);
   return count;
}

static void
surface_notify(struct wl_listener *listener, void *data)
{
   struct wlc_xwm *xwm;
   except((xwm = wl_container_of(listener, xwm, listener.surface)));

   struct wlc_surface_event *ev = data;
   if (ev->type != WLC_SURFACE_EVENT_CREATED)
      return;

   /* Xwayland will send the wayland requests to create the
    * wl_surface before sending this client message.  Even so, we
    * can end up handling the X event before the wayland requests
    * and thus when we try to look up the surface ID, the surface
    * hasn't been created yet. */
   struct wlc_x11_window *win;
   chck_hash_table_for_each(&xwm->unpaired, win) {
      if (!win->surface_id)
         continue;

      link_surface(xwm, win, wl_client_get_object(wlc_xwayland_get_client(), win->surface_id));
   }
}

static void
x11_terminate(struct wlc_xwm *xwm)
{
   if (xwm->cursor)
      xcb_free_cursor(xwm->connection, xwm->cursor);

   /*
   if (xwm->window)
      XCB_CALL(xcb_destroy_window_checked(x11.connection, x11.window));
   */

   if (xwm->connection)
      xcb_disconnect(xwm->connection);
}

static bool
x11_init(struct wlc_xwm *xwm)
{
   if (xwm->connection)
      return true;

   xwm->connection = xcb_connect_to_fd(wlc_xwayland_get_fd(), NULL);
   if (xcb_connection_has_error(xwm->connection))
      goto xcb_connection_fail;

   xcb_prefetch_extension_data(xwm->connection, &xcb_composite_id);

   struct {
      const char *name;
      enum atom_name atom;
   } map[ATOM_LAST] = {
      { "WL_SURFACE_ID", WL_SURFACE_ID },
      { "WM_DELETE_WINDOW", WM_DELETE_WINDOW },
      { "WM_TAKE_FOCUS", WM_TAKE_FOCUS },
      { "WM_PROTOCOLS", WM_PROTOCOLS },
      { "WM_NORMAL_HINTS", WM_NORMAL_HINTS },
      { "_MOTIF_WM_HINTS", MOTIF_WM_HINTS },
      { "TEXT", TEXT },
      { "UTF8_STRING", UTF8_STRING },
      { "CLIPBOARD", CLIPBOARD },
      { "CLIPBOARD_MANAGER", CLIPBOARD_MANAGER },
      { "PRIMARY", PRIMARY },
      { "TARGETS", TARGETS },
      { "WM_S0", WM_S0 },
      { "_NET_WM_CM_S0", NET_WM_S0 },
      { "_NET_WM_PID", NET_WM_PID },
      { "_NET_WM_NAME", NET_WM_NAME },
      { "_NET_WM_STATE", NET_WM_STATE },
      { "_NET_WM_STATE_FULLSCREEN", NET_WM_STATE_FULLSCREEN },
      { "_NET_WM_STATE_MODAL", NET_WM_STATE_MODAL },
      { "_NET_WM_STATE_ABOVE", NET_WM_STATE_ABOVE },
      { "_NET_SUPPORTED", NET_SUPPORTED },
      { "_NET_SUPPORTING_WM_CHECK", NET_SUPPORTING_WM_CHECK },
      { "_NET_WM_WINDOW_TYPE", NET_WM_WINDOW_TYPE },
      { "_NET_WM_WINDOW_TYPE_DESKTOP", NET_WM_WINDOW_TYPE_DESKTOP },
      { "_NET_WM_WINDOW_TYPE_DOCK", NET_WM_WINDOW_TYPE_DOCK },
      { "_NET_WM_WINDOW_TYPE_TOOLBAR", NET_WM_WINDOW_TYPE_TOOLBAR },
      { "_NET_WM_WINDOW_TYPE_MENU", NET_WM_WINDOW_TYPE_MENU },
      { "_NET_WM_WINDOW_TYPE_UTILITY", NET_WM_WINDOW_TYPE_UTILITY },
      { "_NET_WM_WINDOW_TYPE_SPLASH", NET_WM_WINDOW_TYPE_SPLASH },
      { "_NET_WM_WINDOW_TYPE_DIALOG", NET_WM_WINDOW_TYPE_DIALOG },
      { "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", NET_WM_WINDOW_TYPE_DROPDOWN_MENU },
      { "_NET_WM_WINDOW_TYPE_POPUP_MENU", NET_WM_WINDOW_TYPE_POPUP_MENU },
      { "_NET_WM_WINDOW_TYPE_TOOLTIP", NET_WM_WINDOW_TYPE_TOOLTIP },
      { "_NET_WM_WINDOW_TYPE_NOTIFICATION", NET_WM_WINDOW_TYPE_NOTIFICATION },
      { "_NET_WM_WINDOW_TYPE_COMBO", NET_WM_WINDOW_TYPE_COMBO },
      { "_NET_WM_WINDOW_TYPE_DND", NET_WM_WINDOW_TYPE_DND },
      { "_NET_WM_WINDOW_TYPE_NORMAL", NET_WM_WINDOW_TYPE_NORMAL },
   };

   xcb_intern_atom_cookie_t atom_cookies[ATOM_LAST];
   for (uint32_t i = 0; i < ATOM_LAST; ++i)
      atom_cookies[map[i].atom] = xcb_intern_atom(xwm->connection, 0, strlen(map[i].name), map[i].name);

   const xcb_setup_t *setup = xcb_get_setup(xwm->connection);
   xcb_screen_iterator_t screen_iterator = xcb_setup_roots_iterator(setup);
   xwm->screen = screen_iterator.data;

   if (!(xwm->cursor = xcb_generate_id(xwm->connection)))
      goto cursor_fail;

   {
      // Create root cursor
      // XXX: This is same as in gles2.c maybe we need cursor.h

      uint8_t data[] = {
         0x00, 0x00, 0xfe, 0x07, 0xfe, 0x03, 0xfe, 0x01, 0xfe, 0x01, 0xfe, 0x03,
         0xfe, 0x07, 0xfe, 0x0f, 0xfe, 0x1f, 0xe6, 0x0f, 0xc2, 0x07, 0x80, 0x03,
         0x00, 0x01, 0x00, 0x00
      };

      uint8_t mask[] = {
         0xff, 0x3f, 0xff, 0x1f, 0xff, 0x07, 0xff, 0x03, 0xff, 0x03, 0xff, 0x07,
         0xff, 0x0f, 0xff, 0x1f, 0xff, 0x3f, 0xff, 0x1f, 0xe7, 0x0f, 0xc3, 0x07,
         0x83, 0x03, 0x01, 0x01
      };

      xcb_pixmap_t cp = xcb_create_pixmap_from_bitmap_data(xwm->connection, xwm->screen->root, data, 14, 14, 1, 0, 0, 0);
      xcb_pixmap_t mp = xcb_create_pixmap_from_bitmap_data(xwm->connection, xwm->screen->root, mask, 14, 14, 1, 0, 0, 0);
      xcb_create_cursor(xwm->connection, xwm->cursor, cp, mp, 0, 0, 0, 0xFFFF, 0xFFFF, 0xFFFF, 0, 0);
      xcb_free_pixmap(xwm->connection, cp);
      xcb_free_pixmap(xwm->connection, mp);
   }

   uint32_t values[] = { XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_PROPERTY_CHANGE, xwm->cursor };
   if (!XCB_CALL(xwm, xcb_change_window_attributes_checked(xwm->connection, xwm->screen->root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR, values)))
      goto change_attributes_fail;

   const xcb_query_extension_reply_t *composite_extension;
   if (!(composite_extension = xcb_get_extension_data(xwm->connection, &xcb_composite_id)) || !composite_extension->present)
      goto composite_extension_fail;

   if (!XCB_CALL(xwm, xcb_composite_redirect_subwindows_checked(xwm->connection, xwm->screen->root, XCB_COMPOSITE_REDIRECT_MANUAL)))
      goto redirect_subwindows_fail;

   for (uint32_t i = 0; i < ATOM_LAST; ++i) {
      xcb_generic_error_t *error;
      xcb_intern_atom_reply_t *atom_reply = xcb_intern_atom_reply(xwm->connection, atom_cookies[map[i].atom], &error);

      if (atom_reply && !error)
         xwm->atoms[map[i].atom] = atom_reply->atom;

      if (atom_reply)
         free(atom_reply);

      if (error)
         goto atom_get_fail;
   }

   if (!(xwm->window = xcb_generate_id(xwm->connection)))
      goto window_fail;

   XCB_CALL(xwm, xcb_create_window_checked(
               xwm->connection, XCB_COPY_FROM_PARENT, xwm->window, xwm->screen->root,
               0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, xwm->screen->root_visual,
               XCB_CW_EVENT_MASK, (uint32_t[]){XCB_EVENT_MASK_PROPERTY_CHANGE}));

   xcb_atom_t supported[] = {
      xwm->atoms[NET_WM_S0],
      xwm->atoms[NET_WM_PID],
      xwm->atoms[NET_WM_NAME],
      xwm->atoms[NET_WM_STATE],
      xwm->atoms[NET_WM_STATE_FULLSCREEN],
      xwm->atoms[NET_SUPPORTING_WM_CHECK],
      xwm->atoms[NET_WM_WINDOW_TYPE],
      xwm->atoms[NET_WM_WINDOW_TYPE_DESKTOP],
      xwm->atoms[NET_WM_WINDOW_TYPE_DOCK],
      xwm->atoms[NET_WM_WINDOW_TYPE_TOOLBAR],
      xwm->atoms[NET_WM_WINDOW_TYPE_MENU],
      xwm->atoms[NET_WM_WINDOW_TYPE_UTILITY],
      xwm->atoms[NET_WM_WINDOW_TYPE_SPLASH],
      xwm->atoms[NET_WM_WINDOW_TYPE_DIALOG],
      xwm->atoms[NET_WM_WINDOW_TYPE_DROPDOWN_MENU],
      xwm->atoms[NET_WM_WINDOW_TYPE_POPUP_MENU],
      xwm->atoms[NET_WM_WINDOW_TYPE_TOOLTIP],
      xwm->atoms[NET_WM_WINDOW_TYPE_NOTIFICATION],
      xwm->atoms[NET_WM_WINDOW_TYPE_COMBO],
      xwm->atoms[NET_WM_WINDOW_TYPE_DND],
      xwm->atoms[NET_WM_WINDOW_TYPE_NORMAL],
   };

   XCB_CALL(xwm, xcb_change_property_checked(xwm->connection, XCB_PROP_MODE_REPLACE, xwm->screen->root, xwm->atoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, LENGTH(supported), supported));
   XCB_CALL(xwm, xcb_change_property_checked(xwm->connection, XCB_PROP_MODE_REPLACE, xwm->screen->root, xwm->atoms[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &xwm->window));
   XCB_CALL(xwm, xcb_change_property_checked(xwm->connection, XCB_PROP_MODE_REPLACE, xwm->window, xwm->atoms[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &xwm->window));
   XCB_CALL(xwm, xcb_change_property_checked(xwm->connection, XCB_PROP_MODE_REPLACE, xwm->window, xwm->atoms[NET_WM_NAME], xwm->atoms[UTF8_STRING], 8, strlen("xwlc"), "xwlc"));
   XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->connection, xwm->window, xwm->atoms[WM_S0], XCB_CURRENT_TIME));
   XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->connection, xwm->window, xwm->atoms[NET_WM_S0], XCB_CURRENT_TIME));

   xcb_flush(xwm->connection);
   return true;

xcb_connection_fail:
   wlc_log(WLC_LOG_WARN, "Failed to connect to Xwayland");
   goto fail;
composite_extension_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get composite extension");
   goto fail;
cursor_fail:
   wlc_log(WLC_LOG_WARN, "Failed to create empty X11 cursor");
   goto fail;
change_attributes_fail:
   wlc_log(WLC_LOG_WARN, "Failed to change root window attributes");
   goto fail;
redirect_subwindows_fail:
   wlc_log(WLC_LOG_WARN, "Failed to redirect subwindows");
   goto fail;
window_fail:
   wlc_log(WLC_LOG_WARN, "Failed to create wm window");
   goto fail;
atom_get_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get atom");
fail:
   x11_terminate(xwm);
   return false;
}

void
wlc_xwm_release(struct wlc_xwm *xwm)
{
   if (!xwm)
      return;

   if (xwm->event_source) {
      wl_event_source_remove(xwm->event_source);
      wl_list_remove(&xwm->listener.surface.link);
   }

   wlc_xwm_selection_release(xwm);
   chck_hash_table_release(&xwm->unpaired);
   chck_hash_table_release(&xwm->paired);

   x11_terminate(xwm);

   memset(xwm, 0, sizeof(struct wlc_xwm));
}

bool
wlc_xwm(struct wlc_xwm *xwm, struct wlc_seat *seat)
{
   assert(xwm);
   memset(xwm, 0, sizeof(struct wlc_xwm));

   if (!x11_init(xwm))
      return false;

   if (!chck_hash_table(&xwm->paired, 0, 256, sizeof(wlc_handle)) ||
       !chck_hash_table(&xwm->unpaired, 0, 32, sizeof(struct wlc_x11_window)))
      goto fail;

   if (!(xwm->event_source = wl_event_loop_add_fd(wlc_event_loop(), wlc_xwayland_get_fd(), WL_EVENT_READABLE, &x11_event, xwm)))
      goto event_source_fail;

   wl_event_source_check(xwm->event_source);

   xwm->seat = seat;
   xwm->listener.surface.notify = surface_notify;
   wl_signal_add(&wlc_system_signals()->surface, &xwm->listener.surface);
   if (!wlc_xwm_selection_init(xwm))
      goto event_source_fail;

   return true;

event_source_fail:
   wlc_log(WLC_LOG_WARN, "Failed to setup xwm event source");
   goto fail;
fail:
   wlc_xwm_release(xwm);
   return false;
}
