#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>
#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/xcbext.h>
#include <xcb/xcb_image.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <chck/overflow/overflow.h>
#include "internal.h"
#include "macros.h"
#include "xwm.h"
#include "xwayland.h"
#include "compositor/compositor.h"
#include "compositor/view.h"
#include "resources/types/surface.h"

enum atom_name {
   WL_SURFACE_ID,
   WM_DELETE_WINDOW,
   WM_TAKE_FOCUS,
   WM_PROTOCOLS,
   WM_NORMAL_HINTS,
   MOTIF_WM_HINTS,
   UTF8_STRING,
   CLIPBOARD,
   CLIPBOARD_MANAGER,
   WM_S0,
   NET_WM_S0,
   NET_WM_PID,
   NET_WM_NAME,
   NET_WM_STATE,
   NET_WM_STATE_FULLSCREEN,
   NET_WM_STATE_MODAL,
   NET_WM_STATE_ABOVE,
   NET_SUPPORTED,
   NET_SUPPORTING_WM_CHECK,
   NET_WM_WINDOW_TYPE,
   NET_WM_WINDOW_TYPE_DESKTOP,
   NET_WM_WINDOW_TYPE_DOCK,
   NET_WM_WINDOW_TYPE_TOOLBAR,
   NET_WM_WINDOW_TYPE_MENU,
   NET_WM_WINDOW_TYPE_UTILITY,
   NET_WM_WINDOW_TYPE_SPLASH,
   NET_WM_WINDOW_TYPE_DIALOG,
   NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
   NET_WM_WINDOW_TYPE_POPUP_MENU,
   NET_WM_WINDOW_TYPE_TOOLTIP,
   NET_WM_WINDOW_TYPE_NOTIFICATION,
   NET_WM_WINDOW_TYPE_COMBO,
   NET_WM_WINDOW_TYPE_DND,
   NET_WM_WINDOW_TYPE_NORMAL,
   ATOM_LAST
};

static struct {
   xcb_screen_t *screen;
   xcb_connection_t *connection;
   const xcb_query_extension_reply_t *xfixes;
   xcb_atom_t atoms[ATOM_LAST];
   xcb_window_t window, focus;
   xcb_cursor_t cursor;
} x11;

static bool
xcb_call(const char *func, uint32_t line, xcb_void_cookie_t cookie)
{
   xcb_generic_error_t *error;
   if (!(error = xcb_request_check(x11.connection, cookie)))
      return true;

   wlc_log(WLC_LOG_ERROR, "xwm: function %s at line %u x11 error code %d", func, line, error->error_code);
   free(error);
   return false;
}
#define XCB_CALL(x) xcb_call(__PRETTY_FUNCTION__, __LINE__, x)

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

   if (x11.focus == window)
      x11.focus = 0;

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
   win.override_redirect = override_redirect;
   const bool ret = chck_hash_table_set(&xwm->unpaired, window, &win);
   wlc_dlog(WLC_DBG_XWM, "-> Unpaired collisions (%u)", chck_hash_table_collisions(&xwm->unpaired));
   return ret;
}

static struct wlc_view*
view_for_window(struct wlc_x11_window *win)
{
   struct wlc_view *view;
   return (win ? wl_container_of(win, view, x11) : NULL);
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
handle_state(struct wlc_x11_window *win, xcb_atom_t *atoms, size_t nmemb, enum net_wm_state state)
{
   assert(win && atoms);

   struct wlc_view *view;
   if (!(view = view_for_window(win)))
      return;

   for (uint32_t i = 0; i < nmemb; ++i) {
      if (atoms[i] == x11.atoms[NET_WM_STATE_FULLSCREEN]) {
         bool toggle = !(view->pending.state & WLC_BIT_FULLSCREEN);
         wlc_view_request_state(view, WLC_BIT_FULLSCREEN, (state == 0 ? false : (state == 1 ? true : toggle)));
      } else if (atoms[i] == x11.atoms[NET_WM_STATE_MODAL] || atoms[i] == x11.atoms[NET_WM_STATE_ABOVE]) {
         bool toggle = !(view->type & WLC_BIT_MODAL);
         wlc_view_set_type_ptr(view, WLC_BIT_MODAL, (state == 0 ? false : (state == 1 ? true : toggle)));
      }
   }
}

static void
read_properties(struct wlc_xwm *xwm, struct wlc_x11_window *win)
{
   assert(win);

   struct wlc_view *view;
   if (!(view = view_for_window(win)))
      return;

#define TYPE_WM_PROTOCOLS    XCB_ATOM_CUT_BUFFER0
#define TYPE_MOTIF_WM_HINTS  XCB_ATOM_CUT_BUFFER1
#define TYPE_NET_WM_STATE    XCB_ATOM_CUT_BUFFER2
#define TYPE_WM_NORMAL_HINTS XCB_ATOM_CUT_BUFFER3

   const struct {
      xcb_atom_t atom;
      xcb_atom_t type;
   } props[] = {
      { XCB_ATOM_WM_CLASS, XCB_ATOM_STRING },
      { XCB_ATOM_WM_NAME, XCB_ATOM_STRING },
      { XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW },
      { x11.atoms[WM_PROTOCOLS], TYPE_WM_PROTOCOLS },
      { x11.atoms[WM_NORMAL_HINTS], TYPE_WM_NORMAL_HINTS },
      { x11.atoms[NET_WM_STATE], TYPE_NET_WM_STATE },
      { x11.atoms[NET_WM_WINDOW_TYPE], XCB_ATOM_ATOM },
      { x11.atoms[NET_WM_NAME], XCB_ATOM_STRING },
      { x11.atoms[NET_WM_PID], XCB_ATOM_CARDINAL },
      { x11.atoms[MOTIF_WM_HINTS], TYPE_MOTIF_WM_HINTS },
   };

   xcb_get_property_cookie_t *cookies;
   if (!(cookies = chck_calloc_of(LENGTH(props), sizeof(xcb_get_property_cookie_t))))
      return;

   for (uint32_t i = 0; i < LENGTH(props); ++i)
      cookies[i] = xcb_get_property(x11.connection, 0, win->id, props[i].atom, XCB_ATOM_ANY, 0, 2048);

   for (uint32_t i = 0; i < LENGTH(props); ++i) {
      xcb_get_property_reply_t *reply;
      if (!(reply = xcb_get_property_reply(x11.connection, cookies[i], NULL)))
         continue;

      if (reply->type == XCB_ATOM_NONE) {
         free(reply);
         continue;
      }

      switch (props[i].type) {
         case XCB_ATOM_STRING:
            // Class && Name
            if (props[i].atom == XCB_ATOM_WM_CLASS) {
               chck_string_set_cstr_with_length(&view->data._class, xcb_get_property_value(reply), xcb_get_property_value_length(reply), true);
            } else if (props[i].atom == XCB_ATOM_WM_NAME) {
               chck_string_set_cstr_with_length(&view->data.title, xcb_get_property_value(reply), xcb_get_property_value_length(reply), true);
            }
            break;
         case XCB_ATOM_WINDOW:
         {
            // Transient
            xcb_window_t *xid = xcb_get_property_value(reply);
            set_parent(xwm, win, *xid);
         }
         break;
         case XCB_ATOM_CARDINAL:
            // PID
            break;
         case XCB_ATOM_ATOM:
         {
            // Window type
            view->type &= ~WLC_BIT_UNMANAGED | ~WLC_BIT_SPLASH | ~WLC_BIT_MODAL;
            xcb_atom_t *atoms = xcb_get_property_value(reply);
            for (uint32_t i = 0; i < reply->value_len; ++i) {
               if (atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_TOOLTIP] ||
                   atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_UTILITY] ||
                   atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_DND] ||
                   atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_DROPDOWN_MENU] ||
                   atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_POPUP_MENU] ||
                   atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_COMBO]) {
                  wlc_view_set_type_ptr(view, WLC_BIT_UNMANAGED, true);
               }
               if (atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_DIALOG])
                  wlc_view_set_type_ptr(view, WLC_BIT_MODAL, true);
               if (atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_SPLASH])
                  wlc_view_set_type_ptr(view, WLC_BIT_SPLASH, true);
            }
         }
         break;
         case TYPE_WM_PROTOCOLS:
         {
            xcb_atom_t *atoms = xcb_get_property_value(reply);
            for (uint32_t i = 0; i < reply->value_len; ++i) {
               if (atoms[i] == x11.atoms[WM_DELETE_WINDOW])
                  win->has_delete_window = true;
            }
         }
         break;
         case TYPE_WM_NORMAL_HINTS:
            break;
         case TYPE_NET_WM_STATE:
            handle_state(win, xcb_get_property_value(reply), reply->value_len, NET_WM_STATE_ADD);
            break;
         case TYPE_MOTIF_WM_HINTS:
            // Motif hints
            break;
      }

      free(reply);
   }

   free(cookies);

#undef TYPE_WM_PROTOCOLS
#undef TYPE_MOTIF_WM_HINTS
#undef TYPE_NET_WM_STATE
#undef TYPE_WM_NORMAL_HINTS
}

static void
set_geometry(xcb_window_t window, const struct wlc_geometry *g)
{
   assert(g);
   const uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
   const uint32_t values[] = { g->origin.x, g->origin.y, g->size.w, g->size.h };
   wlc_dlog(WLC_DBG_XWM, "-> Configure x11 window (%u) %ux%u+%d,%d", window, g->size.w, g->size.h, g->origin.x, g->origin.y);
   XCB_CALL(xcb_configure_window_checked(x11.connection, window, mask, (uint32_t*)&values));
   xcb_flush(x11.connection);
}

static void
get_geometry(xcb_window_t window, struct wlc_geometry *out_g, uint32_t *out_depth)
{
   if (out_g)
      *out_g = wlc_geometry_zero;

   if (out_depth)
      *out_depth = 0;

   xcb_get_geometry_reply_t *reply;
   if ((reply = xcb_get_geometry_reply(x11.connection, xcb_get_geometry(x11.connection, window), NULL))) {
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
   get_geometry(win->id, &geometry, &depth);
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

   wlc_view_set_type_ptr(view, WLC_BIT_OVERRIDE_REDIRECT, view->x11.override_redirect);
   read_properties(xwm, &view->x11);

   if (!wlc_geometry_equals(&geometry, &wlc_geometry_zero))
      wlc_view_set_geometry_ptr(view, 0, &geometry);

   wlc_dlog(WLC_DBG_XWM, "-> Linked x11 window (%u) to view (%" PRIuWLC ") [%ux%u+%d,%d]",
            view->x11.id, handle, geometry.size.w, geometry.size.h, geometry.origin.x, geometry.origin.y);

   if (!view->parent && x11.focus && (view->type & WLC_BIT_MODAL))
      set_parent(xwm, &view->x11, x11.focus);
}

static void
focus_window(xcb_window_t window, bool force)
{
   if (!force && x11.focus == window)
      return;

   wlc_dlog(WLC_DBG_FOCUS, "-> xwm focus %u", window);

   if (window == 0) {
      XCB_CALL(xcb_set_input_focus_checked(x11.connection, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_NONE, XCB_CURRENT_TIME));
      xcb_flush(x11.connection);
      x11.focus = 0;
      return;
   }

   xcb_client_message_event_t m = {0};
   m.response_type = XCB_CLIENT_MESSAGE;
   m.format = 32;
   m.window = window;
   m.type = x11.atoms[WM_PROTOCOLS];
   m.data.data32[0] = x11.atoms[WM_TAKE_FOCUS];
   m.data.data32[1] = XCB_TIME_CURRENT_TIME;
   XCB_CALL(xcb_send_event_checked(x11.connection, 0, window, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char*)&m));
   XCB_CALL(xcb_set_input_focus_checked(x11.connection, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME));
   XCB_CALL(xcb_configure_window_checked(x11.connection, window, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_ABOVE}));
   xcb_flush(x11.connection);
   x11.focus = window;
}

static void
delete_window(xcb_window_t window)
{
   xcb_client_message_event_t ev = {0};
   ev.response_type = XCB_CLIENT_MESSAGE;
   ev.window = window;
   ev.format = 32;
   ev.sequence = 0;
   ev.type = x11.atoms[WM_PROTOCOLS];
   ev.data.data32[0] = x11.atoms[WM_DELETE_WINDOW];
   ev.data.data32[1] = XCB_CURRENT_TIME;
   XCB_CALL(xcb_send_event_checked(x11.connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (char*)&ev));
}

WLC_PURE enum wlc_surface_format
wlc_x11_window_get_surface_format(struct wlc_x11_window *win)
{
   assert(win);
   return (win->has_alpha ? SURFACE_RGBA : SURFACE_RGB);
}

void
wlc_x11_window_close(struct wlc_x11_window *win)
{
   assert(win);

   if (!win->id)
      return;

   if (win->has_delete_window) {
      delete_window(win->id);
   } else {
      XCB_CALL(xcb_kill_client_checked(x11.connection, win->id));
   }

   xcb_flush(x11.connection);
}

void
wlc_x11_window_configure(struct wlc_x11_window *win, const struct wlc_geometry *g)
{
   assert(win && g);

   if (!win->id)
      return;

   set_geometry(win->id, g);
}

void
wlc_x11_window_set_state(struct wlc_x11_window *win, enum wlc_view_state_bit state, bool toggle)
{
   assert(win);

   if (!win->id)
      return;

   if (state == WLC_BIT_FULLSCREEN)
      XCB_CALL(xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, win->id, x11.atoms[NET_WM_STATE], XCB_ATOM_ATOM, 32, (toggle ? 1 : 0), (toggle ? &x11.atoms[NET_WM_STATE_FULLSCREEN] : NULL)));
}

bool
wlc_x11_window_set_active(struct wlc_x11_window *win, bool active)
{
   assert(win);

   if (!win->id)
      return false;

   if (active) {
      focus_window(win->id, false);
   } else if (win->id == x11.focus) {
      focus_window(0, false);
   }

   return true;
}

static void
handle_client_message(struct wlc_xwm *xwm, xcb_client_message_event_t *ev)
{
   assert(ev);

   if (ev->type == x11.atoms[WL_SURFACE_ID]) {
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

   if (ev->type == x11.atoms[NET_WM_STATE])
      handle_state(win, &ev->data.data32[1], 2, ev->data.data32[0]);
}

static int
x11_event(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;
   struct wlc_xwm *xwm = data;

   int count = 0;
   xcb_generic_event_t *event;
   while ((event = xcb_poll_for_event(x11.connection))) {
      bool xfixes_event = false;
      switch (event->response_type - x11.xfixes->first_event) {
         case XCB_XFIXES_SELECTION_NOTIFY:
            wlc_dlog(WLC_DBG_XWM, "XCB_XFIXES_SELECTION_NOTIFY");
            xfixes_event = true;
            break;
         default: break;
      }

      if (!xfixes_event) {
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
               if (!paired_for_id(xwm, ev->window) && !unpaired_for_id(xwm, ev->window))
                  add_window(xwm, ev->window, ev->override_redirect);
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
               XCB_CALL(xcb_change_window_attributes_checked(x11.connection, ev->window, XCB_CW_EVENT_MASK, &(uint32_t){XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE}));
               XCB_CALL(xcb_map_window_checked(x11.connection, ev->window));
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
               wlc_dlog(WLC_DBG_XWM, "XCB_FOCUS_IN (%u) [%u]", ev->event, x11.focus);
               if (x11.focus && x11.focus != ev->event) {
                  focus_window(x11.focus, true);
               } else {
                  struct wlc_view *view;
                  struct wlc_x11_window *win;
                  if ((win = paired_for_id(xwm, ev->event)) && (view = view_for_window(win))) {
                     struct wlc_focus_event evf = { .view = view, .type = WLC_FOCUS_EVENT_VIEW };
                     wl_signal_emit(&wlc_system_signals()->focus, &evf);
                  }
               }
            }
            break;

            case XCB_PROPERTY_NOTIFY:
            {
               xcb_property_notify_event_t *ev = (xcb_property_notify_event_t*)event;
               wlc_dlog(WLC_DBG_XWM, "XCB_PROPERTY_NOTIFY (%u)", ev->window);
               struct wlc_x11_window *win;
               if ((win = paired_for_id(xwm, ev->window)))
                  read_properties(xwm, win);
            }
            break;

            case XCB_CONFIGURE_REQUEST:
            {
               xcb_configure_request_event_t *ev = (xcb_configure_request_event_t*)event;
               wlc_dlog(WLC_DBG_XWM, "XCB_CONFIGURE_REQUEST (%u) [%ux%u+%d,%d]", ev->window, ev->width, ev->height, ev->x, ev->y);

               // Some windows freeze unless they get what they want.
               const struct wlc_geometry r = { { ev->x, ev->y }, { ev->width, ev->height } };
               set_geometry(ev->window, &r);

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
               if ((win = paired_for_id(xwm, ev->window)) && (view = view_for_window(win))) {
                  if (win->override_redirect != ev->override_redirect)
                     wlc_view_set_type_ptr(view, WLC_BIT_OVERRIDE_REDIRECT, (win->override_redirect = ev->override_redirect));
               }
            }
            break;

            // TODO: Handle?
            case XCB_SELECTION_NOTIFY:
               wlc_dlog(WLC_DBG_XWM, "XCB_SELECTION_NOTIFY");
               break;
            case XCB_SELECTION_REQUEST:
               wlc_dlog(WLC_DBG_XWM, "XCB_SELECTION_REQUEST");
               break;
            case XCB_FOCUS_OUT:
               wlc_dlog(WLC_DBG_XWM, "XCB_FOCUS_OUT");
               break;
            case XCB_MAPPING_NOTIFY:
               wlc_dlog(WLC_DBG_XWM, "XCB_MAPPING_NOTIFY");
               break;

            default:
               wlc_log(WLC_LOG_WARN, "xwm: unimplemented %d", event->response_type & ~0x80);
               break;
         }
      }

      free(event);
      count += 1;
   }

   xcb_flush(x11.connection);
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
x11_terminate(void)
{
   if (x11.cursor)
      xcb_free_cursor(x11.connection, x11.cursor);

   if (x11.window)
      XCB_CALL(xcb_destroy_window_checked(x11.connection, x11.window));

   if (x11.connection)
      xcb_disconnect(x11.connection);

   memset(&x11, 0, sizeof(x11));
}

static bool
x11_init(void)
{
   if (x11.connection)
      return true;

   x11.connection = xcb_connect_to_fd(wlc_xwayland_get_fd(), NULL);
   if (xcb_connection_has_error(x11.connection))
      goto xcb_connection_fail;

   xcb_prefetch_extension_data(x11.connection, &xcb_composite_id);

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
      { "UTF8_STRING", UTF8_STRING },
      { "CLIPBOARD", CLIPBOARD },
      { "CLIPBOARD_MANAGER", CLIPBOARD_MANAGER },
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
      atom_cookies[map[i].atom] = xcb_intern_atom(x11.connection, 0, strlen(map[i].name), map[i].name);

   const xcb_setup_t *setup = xcb_get_setup(x11.connection);
   xcb_screen_iterator_t screen_iterator = xcb_setup_roots_iterator(setup);
   x11.screen = screen_iterator.data;

   if (!(x11.cursor = xcb_generate_id(x11.connection)))
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

      xcb_pixmap_t cp = xcb_create_pixmap_from_bitmap_data(x11.connection, x11.screen->root, data, 14, 14, 1, 0, 0, 0);
      xcb_pixmap_t mp = xcb_create_pixmap_from_bitmap_data(x11.connection, x11.screen->root, mask, 14, 14, 1, 0, 0, 0);
      xcb_create_cursor(x11.connection, x11.cursor, cp, mp, 0, 0, 0, 0xFFFF, 0xFFFF, 0xFFFF, 0, 0);
      xcb_free_pixmap(x11.connection, cp);
      xcb_free_pixmap(x11.connection, mp);
   }

   uint32_t values[] = { XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_PROPERTY_CHANGE, x11.cursor };
   if (!XCB_CALL(xcb_change_window_attributes_checked(x11.connection, x11.screen->root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR, values)))
      goto change_attributes_fail;

   if (!(x11.xfixes = xcb_get_extension_data(x11.connection, &xcb_xfixes_id)) || !x11.xfixes->present)
      goto xfixes_extension_fail;

   xcb_xfixes_query_version_reply_t *xfixes_reply;
   if (!(xfixes_reply = xcb_xfixes_query_version_reply(x11.connection, xcb_xfixes_query_version(x11.connection, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION), NULL)))
      goto xfixes_extension_fail;

   wlc_log(WLC_LOG_INFO, "xfixes (%d.%d)", xfixes_reply->major_version, xfixes_reply->minor_version);
   free(xfixes_reply);

   const xcb_query_extension_reply_t *composite_extension;
   if (!(composite_extension = xcb_get_extension_data(x11.connection, &xcb_composite_id)) || !composite_extension->present)
      goto composite_extension_fail;

   if (!XCB_CALL(xcb_composite_redirect_subwindows_checked(x11.connection, x11.screen->root, XCB_COMPOSITE_REDIRECT_MANUAL)))
      goto redirect_subwindows_fail;

   for (uint32_t i = 0; i < ATOM_LAST; ++i) {
      xcb_generic_error_t *error;
      xcb_intern_atom_reply_t *atom_reply = xcb_intern_atom_reply(x11.connection, atom_cookies[map[i].atom], &error);

      if (atom_reply && !error)
         x11.atoms[map[i].atom] = atom_reply->atom;

      if (atom_reply)
         free(atom_reply);

      if (error)
         goto atom_get_fail;
   }

   if (!(x11.window = xcb_generate_id(x11.connection)))
      goto window_fail;

   XCB_CALL(xcb_create_window_checked(
               x11.connection, XCB_COPY_FROM_PARENT, x11.window, x11.screen->root,
               0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual,
               XCB_CW_EVENT_MASK, (uint32_t[]){XCB_EVENT_MASK_PROPERTY_CHANGE}));

   xcb_atom_t supported[] = {
      x11.atoms[NET_WM_S0],
      x11.atoms[NET_WM_PID],
      x11.atoms[NET_WM_NAME],
      x11.atoms[NET_WM_STATE],
      x11.atoms[NET_WM_STATE_FULLSCREEN],
      x11.atoms[NET_SUPPORTING_WM_CHECK],
      x11.atoms[NET_WM_WINDOW_TYPE],
      x11.atoms[NET_WM_WINDOW_TYPE_DESKTOP],
      x11.atoms[NET_WM_WINDOW_TYPE_DOCK],
      x11.atoms[NET_WM_WINDOW_TYPE_TOOLBAR],
      x11.atoms[NET_WM_WINDOW_TYPE_MENU],
      x11.atoms[NET_WM_WINDOW_TYPE_UTILITY],
      x11.atoms[NET_WM_WINDOW_TYPE_SPLASH],
      x11.atoms[NET_WM_WINDOW_TYPE_DIALOG],
      x11.atoms[NET_WM_WINDOW_TYPE_DROPDOWN_MENU],
      x11.atoms[NET_WM_WINDOW_TYPE_POPUP_MENU],
      x11.atoms[NET_WM_WINDOW_TYPE_TOOLTIP],
      x11.atoms[NET_WM_WINDOW_TYPE_NOTIFICATION],
      x11.atoms[NET_WM_WINDOW_TYPE_COMBO],
      x11.atoms[NET_WM_WINDOW_TYPE_DND],
      x11.atoms[NET_WM_WINDOW_TYPE_NORMAL],
   };

   XCB_CALL(xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, x11.screen->root, x11.atoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, LENGTH(supported), supported));
   XCB_CALL(xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, x11.screen->root, x11.atoms[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &x11.window));
   XCB_CALL(xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, x11.window, x11.atoms[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &x11.window));
   XCB_CALL(xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, x11.window, x11.atoms[NET_WM_NAME], x11.atoms[UTF8_STRING], 8, strlen("xwlc"), "xwlc"));
   XCB_CALL(xcb_set_selection_owner_checked(x11.connection, x11.window, x11.atoms[CLIPBOARD_MANAGER], XCB_CURRENT_TIME));
   XCB_CALL(xcb_set_selection_owner_checked(x11.connection, x11.window, x11.atoms[WM_S0], XCB_CURRENT_TIME));
   XCB_CALL(xcb_set_selection_owner_checked(x11.connection, x11.window, x11.atoms[NET_WM_S0], XCB_CURRENT_TIME));

   uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
   XCB_CALL(xcb_xfixes_select_selection_input_checked(x11.connection, x11.window, x11.atoms[CLIPBOARD], mask));

   xcb_flush(x11.connection);
   return true;

xcb_connection_fail:
   wlc_log(WLC_LOG_WARN, "Failed to connect to Xwayland");
   goto fail;
xfixes_extension_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get xfixes extension");
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
   x11_terminate();
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

   chck_hash_table_release(&xwm->unpaired);
   chck_hash_table_release(&xwm->paired);

   // XXX: We currently allow only single compositor && xwm
   //      but this will not work if we ever allow multiple.
   x11_terminate();

   memset(xwm, 0, sizeof(struct wlc_xwm));
}

bool
wlc_xwm(struct wlc_xwm *xwm)
{
   assert(xwm);
   memset(xwm, 0, sizeof(struct wlc_xwm));

   if (!x11_init())
      return false;

   if (!chck_hash_table(&xwm->paired, 0, 256, sizeof(wlc_handle)) ||
       !chck_hash_table(&xwm->unpaired, 0, 32, sizeof(struct wlc_x11_window)))
      goto fail;

   if (!(xwm->event_source = wl_event_loop_add_fd(wlc_event_loop(), wlc_xwayland_get_fd(), WL_EVENT_READABLE, &x11_event, xwm)))
      goto event_source_fail;

   wl_event_source_check(xwm->event_source);

   xwm->listener.surface.notify = surface_notify;
   wl_signal_add(&wlc_system_signals()->surface, &xwm->listener.surface);
   return true;

event_source_fail:
   wlc_log(WLC_LOG_WARN, "Failed to setup xwm event source");
   goto fail;
fail:
   wlc_xwm_release(xwm);
   return false;
}
