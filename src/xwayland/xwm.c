#include "wlc_internal.h"
#include "xwm.h"

#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "compositor/output.h"
#include "compositor/view.h"

#include "seat/seat.h"
#include "seat/client.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>

#include <xcb/composite.h>
#include <xcb/xfixes.h>

#include <wayland-server.h>
#include <wayland-util.h>

#define LENGTH(x) (sizeof(x) / sizeof(x)[0])

// FIXME: contains global state

struct wlc_x11_window {
   struct wlc_view *view;
   struct wl_list link;
   xcb_window_t id;
   uint32_t surface_id;
   bool override_redirect;
   bool has_delete_window;
   bool has_alpha;
};

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
   struct wlc_compositor *compositor;
   struct wl_client *client;
   struct wl_event_source *event_source;
   struct wl_list windows, unpaired_windows;
   xcb_window_t focus;
} xwm;

static struct {
   xcb_screen_t *screen;
   xcb_connection_t *connection;
   const xcb_query_extension_reply_t *xfixes;
   xcb_atom_t atoms[ATOM_LAST];
   xcb_window_t window;

   struct {
      void *xcb_handle;

      xcb_connection_t* (*xcb_connect_to_fd)(int, xcb_auth_info_t*);
      void (*xcb_disconnect)(xcb_connection_t*);
      void (*xcb_prefetch_extension_data)(xcb_connection_t*, xcb_extension_t*);
      int (*xcb_flush)(xcb_connection_t*);
      int (*xcb_connection_has_error)(xcb_connection_t*);
      const xcb_setup_t* (*xcb_get_setup)(xcb_connection_t*);
      xcb_screen_iterator_t (*xcb_setup_roots_iterator)(const xcb_setup_t*);
      uint32_t (*xcb_generate_id)(xcb_connection_t*);
      xcb_get_property_cookie_t (*xcb_get_property)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint32_t, uint32_t);
      xcb_get_property_reply_t* (*xcb_get_property_reply)(xcb_connection_t*, xcb_get_property_cookie_t, xcb_generic_error_t**);
      void* (*xcb_get_property_value)(xcb_get_property_reply_t*);
      int (*xcb_get_property_value_length)(xcb_get_property_reply_t*);
      xcb_get_geometry_cookie_t (*xcb_get_geometry)(xcb_connection_t*, xcb_drawable_t);
      xcb_get_geometry_reply_t* (*xcb_get_geometry_reply)(xcb_connection_t*, xcb_get_geometry_cookie_t, xcb_generic_error_t**);
      xcb_void_cookie_t (*xcb_create_window_checked)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_window_t, int16_t, int16_t, uint16_t, uint16_t, uint16_t, uint16_t, xcb_visualid_t, uint32_t, const uint32_t*);
      xcb_void_cookie_t (*xcb_destroy_window_checked)(xcb_connection_t*, xcb_window_t);
      xcb_void_cookie_t (*xcb_map_window_checked)(xcb_connection_t*, xcb_window_t);
      xcb_void_cookie_t (*xcb_unmap_window_checked)(xcb_connection_t*, xcb_window_t);
      xcb_void_cookie_t (*xcb_change_property_checked)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint8_t, uint32_t, const void*);
      xcb_void_cookie_t (*xcb_change_window_attributes_checked)(xcb_connection_t*, xcb_window_t, uint32_t, const uint32_t*);
      xcb_void_cookie_t (*xcb_configure_window_checked)(xcb_connection_t*, xcb_window_t, uint16_t, const uint32_t*);
      xcb_void_cookie_t (*xcb_set_selection_owner_checked)(xcb_connection_t*, xcb_window_t, xcb_atom_t, xcb_timestamp_t);
      xcb_void_cookie_t (*xcb_set_input_focus_checked)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_timestamp_t);
      xcb_void_cookie_t (*xcb_kill_client_checked)(xcb_connection_t*, uint32_t);
      xcb_void_cookie_t (*xcb_send_event_checked)(xcb_connection_t*, uint8_t, xcb_window_t, uint32_t, const char*);
      xcb_intern_atom_cookie_t (*xcb_intern_atom)(xcb_connection_t*, uint8_t, uint16_t, const char*);
      xcb_intern_atom_reply_t* (*xcb_intern_atom_reply)(xcb_connection_t*, xcb_intern_atom_cookie_t, xcb_generic_error_t**);
      xcb_generic_error_t* (*xcb_request_check)(xcb_connection_t*, xcb_void_cookie_t);
      xcb_generic_event_t* (*xcb_poll_for_event)(xcb_connection_t*);
      xcb_query_extension_reply_t* (*xcb_get_extension_data)(xcb_connection_t*, xcb_extension_t*);

      xcb_void_cookie_t (*xcb_composite_redirect_subwindows_checked)(xcb_connection_t*, xcb_window_t, uint8_t);
      xcb_extension_t *xcb_composite_id;

      xcb_xfixes_query_version_cookie_t (*xcb_xfixes_query_version)(xcb_connection_t*, uint32_t, uint32_t);
      xcb_xfixes_query_version_reply_t* (*xcb_xfixes_query_version_reply)(xcb_connection_t*, xcb_xfixes_query_version_cookie_t, xcb_generic_error_t**);
      xcb_void_cookie_t (*xcb_xfixes_select_selection_input_checked)(xcb_connection_t*, xcb_window_t, xcb_atom_t, uint32_t);
      xcb_extension_t *xcb_xfixes_id;
   } api;
} x11;

static bool
xcb_load(void)
{
   const char *lib = "libxcb.so", *func = NULL;

   if (!(x11.api.xcb_handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (x11.api.x = dlsym(x11.api.xcb_handle, (func = #x)))

   if (!load(xcb_connect_to_fd))
      goto function_pointer_exception;
   if (!load(xcb_prefetch_extension_data))
      goto function_pointer_exception;
   if (!load(xcb_flush))
      goto function_pointer_exception;
   if (!load(xcb_connection_has_error))
      goto function_pointer_exception;
   if (!load(xcb_get_setup))
      goto function_pointer_exception;
   if (!load(xcb_setup_roots_iterator))
      goto function_pointer_exception;
   if (!load(xcb_generate_id))
      goto function_pointer_exception;
   if (!load(xcb_get_property))
      goto function_pointer_exception;
   if (!load(xcb_get_property_reply))
      goto function_pointer_exception;
   if (!load(xcb_get_property_value))
      goto function_pointer_exception;
   if (!load(xcb_get_property_value_length))
      goto function_pointer_exception;
   if (!load(xcb_get_geometry))
      goto function_pointer_exception;
   if (!load(xcb_get_geometry_reply))
      goto function_pointer_exception;
   if (!load(xcb_create_window_checked))
      goto function_pointer_exception;
   if (!load(xcb_destroy_window_checked))
      goto function_pointer_exception;
   if (!load(xcb_map_window_checked))
      goto function_pointer_exception;
   if (!load(xcb_unmap_window_checked))
      goto function_pointer_exception;
   if (!load(xcb_change_property_checked))
      goto function_pointer_exception;
   if (!load(xcb_change_window_attributes_checked))
      goto function_pointer_exception;
   if (!load(xcb_configure_window_checked))
      goto function_pointer_exception;
   if (!load(xcb_set_selection_owner_checked))
      goto function_pointer_exception;
   if (!load(xcb_set_input_focus_checked))
      goto function_pointer_exception;
   if (!load(xcb_kill_client_checked))
      goto function_pointer_exception;
   if (!load(xcb_send_event_checked))
      goto function_pointer_exception;
   if (!load(xcb_intern_atom))
      goto function_pointer_exception;
   if (!load(xcb_intern_atom_reply))
      goto function_pointer_exception;
   if (!load(xcb_request_check))
      goto function_pointer_exception;
   if (!load(xcb_poll_for_event))
      goto function_pointer_exception;
   if (!load(xcb_get_extension_data))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   wlc_log(WLC_LOG_WARN, "Could not load function '%s' from '%s'", func, lib);
   return false;
}

static bool
xcb_composite_load(void)
{
   const char *lib = "libxcb-composite.so", *func = NULL;

   if (!(x11.api.xcb_handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (x11.api.x = dlsym(x11.api.xcb_handle, (func = #x)))

   if (!load(xcb_composite_redirect_subwindows_checked))
      goto function_pointer_exception;
   if (!load(xcb_composite_id))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   wlc_log(WLC_LOG_WARN, "Could not load function '%s' from '%s'", func, lib);
   return false;
}

static bool
xcb_xfixes_load(void)
{
   const char *lib = "libxcb-xfixes.so", *func = NULL;

   if (!(x11.api.xcb_handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (x11.api.x = dlsym(x11.api.xcb_handle, (func = #x)))

   if (!load(xcb_xfixes_query_version))
      goto function_pointer_exception;
   if (!load(xcb_xfixes_query_version_reply))
      goto function_pointer_exception;
   if (!load(xcb_xfixes_select_selection_input_checked))
      goto function_pointer_exception;
   if (!load(xcb_xfixes_id))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   wlc_log(WLC_LOG_WARN, "Could not load function '%s' from '%s'", func, lib);
   return false;
}

static bool
xcb_call(const char *func, uint32_t line, xcb_void_cookie_t cookie)
{
   xcb_generic_error_t *error;
   if (!(error = x11.api.xcb_request_check(x11.connection, cookie)))
      return true;

   wlc_log(WLC_LOG_ERROR, "xwm: function %s at line %u x11 error code %d", func, line, error->error_code);
   return false;
}
#define XCB_CALL(x) xcb_call(__PRETTY_FUNCTION__, __LINE__, x)

/**
 * TODO: change to hashmap, instead of wl_list
 */
static struct wlc_x11_window*
wlc_x11_window_for_id(struct wl_list *list, xcb_window_t window)
{
   struct wlc_x11_window *win;
   wl_list_for_each(win, list, link) {
      if (win->id == window)
         return win;
   }
   return NULL;
}

static void
set_parent(struct wlc_x11_window *win, xcb_window_t parent_id)
{
   assert(win && win->view);

   if (!parent_id) {
      wlc_view_set_parent(win->view, NULL);
      return;
   }

   struct wlc_x11_window *parent;
   if ((parent = wlc_x11_window_for_id(&xwm.windows, parent_id)) && parent->view)
      wlc_view_set_parent(win->view, parent->view);
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

#define BIT_TOGGLE(w, m, f) (w & ~m) | (-f & m)
   for (uint32_t i = 0; i < nmemb; ++i) {
      if (atoms[i] == x11.atoms[NET_WM_STATE_FULLSCREEN]) {
         bool toggle = !(win->view->pending.state & WLC_BIT_FULLSCREEN);
         wlc_view_request_state(win->view, WLC_BIT_FULLSCREEN, (state == 0 ? false : (state == 1 ? true : toggle)));
      } else if (atoms[i] == x11.atoms[NET_WM_STATE_MODAL] || atoms[i] == x11.atoms[NET_WM_STATE_ABOVE]) {
         bool toggle = !(win->view->type & WLC_BIT_UNMANAGED);
         win->view->type = BIT_TOGGLE(win->view->type, WLC_BIT_UNMANAGED, (state == 0 ? false : (state == 1 ? true : toggle)));
      }
   }
#undef BIT_TOGGLE
}

static void
read_properties(struct wlc_x11_window *win)
{
   assert(win);

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
   if (!(cookies = calloc(LENGTH(props), sizeof(xcb_get_property_cookie_t))))
      return;

   for (uint32_t i = 0; i < LENGTH(props); ++i)
      cookies[i] = x11.api.xcb_get_property(x11.connection, 0, win->id, props[i].atom, XCB_ATOM_ANY, 0, 2048);

   for (uint32_t i = 0; i < LENGTH(props); ++i) {
      xcb_get_property_reply_t *reply;
      if (!(reply = x11.api.xcb_get_property_reply(x11.connection, cookies[i], NULL)))
         continue;

      if (reply->type == XCB_ATOM_NONE) {
         free(reply);
         continue;
      }

      switch (props[i].type) {
         case XCB_ATOM_STRING:
            // Class && Name
            if (props[i].atom == XCB_ATOM_WM_CLASS) {
               wlc_string_set_with_length(&win->view->shell_surface._class, x11.api.xcb_get_property_value(reply), x11.api.xcb_get_property_value_length(reply));
            } else if (props[i].atom == XCB_ATOM_WM_NAME) {
               wlc_string_set_with_length(&win->view->shell_surface.title, x11.api.xcb_get_property_value(reply), x11.api.xcb_get_property_value_length(reply));
            }
            break;
         case XCB_ATOM_WINDOW: {
            // Transient
            xcb_window_t *xid = x11.api.xcb_get_property_value(reply);
            set_parent(win, *xid);
         }
         break;
         case XCB_ATOM_CARDINAL:
         // PID
         break;
         case XCB_ATOM_ATOM: {
            // Window type
            win->view->type &= ~WLC_BIT_UNMANAGED | ~WLC_BIT_SPLASH;
            xcb_atom_t *atoms = x11.api.xcb_get_property_value(reply);
            for (uint32_t i = 0; i < reply->value_len; ++i) {
               if (atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_TOOLTIP]  ||
                   atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_DND] ||
                   atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_MENU])
                  win->view->type |= WLC_BIT_UNMANAGED;
               if (atoms[i] == x11.atoms[NET_WM_WINDOW_TYPE_SPLASH])
                  win->view->type |= WLC_BIT_SPLASH;
            }
         }
         break;
         case TYPE_WM_PROTOCOLS: {
            xcb_atom_t *atoms = x11.api.xcb_get_property_value(reply);
            for (uint32_t i = 0; i < reply->value_len; ++i) {
               if (atoms[i] == x11.atoms[WM_DELETE_WINDOW])
                  win->has_delete_window = true;
            }
         }
         break;
         case TYPE_WM_NORMAL_HINTS:
         break;
         case TYPE_NET_WM_STATE:
            handle_state(win, x11.api.xcb_get_property_value(reply), reply->value_len, NET_WM_STATE_ADD);
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
link_surface(struct wlc_x11_window *win, struct wl_resource *resource)
{
   assert(win);

   if (!resource)
      return;

   struct wlc_client *client;
   if (!(client = wlc_client_for_client_with_wl_client_in_list(xwm.client, &xwm.compositor->clients))) {
      wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "Could not find wlc_client for wl_client");
      return;
   }

   struct wlc_surface *surface = wl_resource_get_user_data(resource);
   if (!surface->view && !(surface->view = wlc_view_new(xwm.compositor, client, surface))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   win->view = surface->view;
   win->view->x11_window = win;

   if (win->override_redirect)
      win->view->type |= WLC_BIT_OVERRIDE_REDIRECT;

   read_properties(win);

   xcb_get_geometry_reply_t *reply;
   if ((reply = x11.api.xcb_get_geometry_reply(x11.connection, x11.api.xcb_get_geometry(x11.connection, win->id), NULL))) {
      win->view->pending.geometry.origin = (struct wlc_origin){ reply->x, reply->y };
      win->view->pending.geometry.size = (struct wlc_size){ reply->width, reply->height };
      win->has_alpha = (reply->depth == 32);
      free(reply);
   }

   if (surface->output) {
      // Make sure the view is mapped to space.
      // The timing with xwayland <-> wayland events might be bit off, so this does not hurt.
      wlc_view_set_space(win->view, surface->output->space);
   }

   wl_list_remove(&win->link);
   wl_list_insert(&xwm.windows, &win->link);
}

static void
focus_window(xcb_window_t window)
{
   if (xwm.focus == window)
      return;

   wlc_dlog(WLC_DBG_FOCUS, "-> xwm focus %u", window);

   if (window == 0) {
      XCB_CALL(x11.api.xcb_set_input_focus_checked(x11.connection, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_NONE, XCB_CURRENT_TIME));
      xwm.focus = 0;
      return;
   }

   xcb_client_message_event_t m;
   m.response_type = XCB_CLIENT_MESSAGE;
   m.format = 32;
   m.window = window;
   m.type = x11.atoms[WM_PROTOCOLS];
   m.data.data32[0] = x11.atoms[WM_TAKE_FOCUS];
   m.data.data32[1] = XCB_TIME_CURRENT_TIME;
   XCB_CALL(x11.api.xcb_send_event_checked(x11.connection, 0, window, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char*)&m));
   XCB_CALL(x11.api.xcb_set_input_focus_checked(x11.connection, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME));
   x11.api.xcb_flush(x11.connection);
   xwm.focus = window;
}

static struct wlc_x11_window*
wlc_x11_window_new(xcb_window_t window, bool override_redirect)
{
   struct wlc_x11_window *win;
   if (!(win = calloc(1, sizeof(struct wlc_x11_window))))
      return NULL;

   win->id = window;
   win->override_redirect = override_redirect;
   wl_list_insert(&xwm.unpaired_windows, &win->link);
   return win;
}

void
wlc_x11_window_free(struct wlc_x11_window *win)
{
   assert(win);

   if (xwm.focus == win->id)
      focus_window(0);

   if (win->view) {
      wlc_view_defocus(win->view);
      win->view->x11_window = NULL;
      win->view->client = NULL;
   }

   wl_list_remove(&win->link);
   free(win);
}

static void
deletewindow(xcb_window_t window)
{
   xcb_client_message_event_t ev;
   ev.response_type = XCB_CLIENT_MESSAGE;
   ev.window = window;
   ev.format = 32;
   ev.sequence = 0;
   ev.type = x11.atoms[WM_PROTOCOLS];
   ev.data.data32[0] = x11.atoms[WM_DELETE_WINDOW];
   ev.data.data32[1] = XCB_CURRENT_TIME;
   XCB_CALL(x11.api.xcb_send_event_checked(x11.connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (char*)&ev));
}

enum wlc_surface_format
wlc_x11_window_get_surface_format(struct wlc_x11_window *win)
{
   assert(win);
   return (win->has_alpha ? SURFACE_RGBA : SURFACE_RGB);
}

void
wlc_x11_window_close(struct wlc_x11_window *win)
{
   assert(win);

   if (win->has_delete_window) {
      deletewindow(win->id);
   } else {
      XCB_CALL(x11.api.xcb_kill_client_checked(x11.connection, win->id));
   }

   x11.api.xcb_flush(x11.connection);
}

void
wlc_x11_window_position(struct wlc_x11_window *win, const int32_t x, const int32_t y)
{
   assert(win);
   static const uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
   const uint32_t values[] = { x, y };
   XCB_CALL(x11.api.xcb_configure_window_checked(x11.connection, win->id, mask, (uint32_t*)&values));
   x11.api.xcb_flush(x11.connection);
}

void
wlc_x11_window_resize(struct wlc_x11_window *win, const uint32_t width, const uint32_t height)
{
   assert(win);
   static const uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
   const uint32_t values[] = { width, height };
   XCB_CALL(x11.api.xcb_configure_window_checked(x11.connection, win->id, mask, (uint32_t*)&values));
   x11.api.xcb_flush(x11.connection);
}

void
wlc_x11_window_set_state(struct wlc_x11_window *win, enum wlc_view_state_bit state, bool toggle)
{
   assert(win);

   if (state == WLC_BIT_FULLSCREEN)
      XCB_CALL(x11.api.xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, win->id, x11.atoms[NET_WM_STATE], XCB_ATOM_ATOM, 32, (toggle ? 1 : 0), (toggle ? &x11.atoms[NET_WM_STATE_FULLSCREEN] : NULL)));
}

void
wlc_x11_window_set_active(struct wlc_x11_window *win, bool active)
{
   assert(win);

   if (active) {
      focus_window(win->id);
   } else if (win->id == xwm.focus) {
      focus_window(0);
   }
}

static void
handle_client_message(xcb_client_message_event_t *ev)
{
   assert(ev);

   if (ev->type == x11.atoms[WL_SURFACE_ID]) {
      struct wlc_x11_window *win;
      if (!(win = wlc_x11_window_for_id(&xwm.unpaired_windows, ev->window)))
         return;

      link_surface(win, wl_client_get_object(xwm.client, ev->data.data32[0]));
      win->surface_id = ev->data.data32[0];
      return;
   }

   struct wlc_x11_window *win;
   if (!(win = wlc_x11_window_for_id(&xwm.windows, ev->window)) || !win->view)
      return;

   if (ev->type == x11.atoms[NET_WM_STATE])
      handle_state(win, &ev->data.data32[1], 2, ev->data.data32[0]);
}

static int
x11_event(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask, (void)data;

   int count = 0;
   xcb_generic_event_t *event;
   while ((event = x11.api.xcb_poll_for_event(x11.connection))) {
      bool xfixes_event = false;
      switch (event->response_type - x11.xfixes->first_event) {
         case XCB_XFIXES_SELECTION_NOTIFY:
         xfixes_event = true;
         break;
         default:break;
      }

      if (!xfixes_event) {
         switch (event->response_type & ~0x80) {
            case 0:
               wlc_log(WLC_LOG_ERROR, "xwm: Uncatched X11 error occured");
               break;

            case XCB_CREATE_NOTIFY: {
               xcb_create_notify_event_t *ev = (xcb_create_notify_event_t*)event;
               wlc_x11_window_new(ev->window, ev->override_redirect);
            }
            break;

            case XCB_MAP_NOTIFY: {
               xcb_map_notify_event_t *ev = (xcb_map_notify_event_t*)event;
               if (!wlc_x11_window_for_id(&xwm.windows, ev->window) && !wlc_x11_window_for_id(&xwm.unpaired_windows, ev->window))
                  wlc_x11_window_new(ev->window, ev->override_redirect);
            }
            break;

            case XCB_DESTROY_NOTIFY: {
               xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t*)event;
               struct wlc_x11_window *win;
               if ((win = wlc_x11_window_for_id(&xwm.windows, ev->window)) || (win = wlc_x11_window_for_id(&xwm.unpaired_windows, ev->window)))
                  wlc_x11_window_free(win);
            }
            break;

            case XCB_MAP_REQUEST: {
               xcb_map_request_event_t *ev = (xcb_map_request_event_t*)event;
               XCB_CALL(x11.api.xcb_change_window_attributes_checked(x11.connection, ev->window, XCB_CW_EVENT_MASK, &(uint32_t){XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE}));
               XCB_CALL(x11.api.xcb_map_window_checked(x11.connection, ev->window));
            }
            break;

            case XCB_CLIENT_MESSAGE:
               handle_client_message((xcb_client_message_event_t*)event);
            break;

            case XCB_FOCUS_IN: {
               // Do not let clients to steal focus
               xcb_focus_in_event_t *ev = (xcb_focus_in_event_t*)event;
               if (xwm.focus && xwm.focus != ev->event)
                  focus_window(xwm.focus);
            }
            break;

            case XCB_PROPERTY_NOTIFY: {
               xcb_property_notify_event_t *ev = (xcb_property_notify_event_t*)event;
               struct wlc_x11_window *win;
               if ((win = wlc_x11_window_for_id(&xwm.windows, ev->window)))
                  read_properties(win);
            }
            break;

            case XCB_CONFIGURE_REQUEST: {
               xcb_configure_request_event_t *ev = (xcb_configure_request_event_t*)event;
               struct wlc_x11_window *win;
               if ((win = wlc_x11_window_for_id(&xwm.windows, ev->window)) && win->view) {
                  set_parent(win, ev->parent);
                  struct wlc_geometry r = { { ev->x, ev->y }, { ev->width, ev->height } };
                  wlc_view_request_geometry(win->view, &r);
               }

            }
            break;

            case XCB_CONFIGURE_NOTIFY: {
               // XXX: Maybe we could ac here?
               xcb_configure_notify_event_t *ev = (xcb_configure_notify_event_t*)event;
               struct wlc_x11_window *win;
               if ((win = wlc_x11_window_for_id(&xwm.windows, ev->window)) && win->view) {
                  // win->view->ack = ACK_NEXT_COMMIT;

                  if (win->override_redirect != ev->override_redirect) {
                     if (ev->override_redirect)
                        win->view->type |= WLC_BIT_OVERRIDE_REDIRECT;
                     else
                        win->view->type &= ~WLC_BIT_OVERRIDE_REDIRECT;
                     win->override_redirect = ev->override_redirect;
                  }
               }
            }
            break;

            // TODO: Handle?
            case XCB_SELECTION_NOTIFY:
            break;
            case XCB_SELECTION_REQUEST:
            break;
            case XCB_FOCUS_OUT:
            break;
            case XCB_MAPPING_NOTIFY:
            break;
            case XCB_UNMAP_NOTIFY:
            break;

            default:
               wlc_log(WLC_LOG_WARN, "xwm: unimplemented %d", event->response_type & ~0x80);
            break;
         }
      }

      free(event);
      count += 1;
   }

   x11.api.xcb_flush(x11.connection);
   return count;
}

void
wlc_xwm_surface_notify(struct wlc_compositor *compositor)
{
   (void)compositor;

   if (!xwm.compositor)
      return;

   /* Xwayland will send the wayland requests to create the
    * wl_surface before sending this client message.  Even so, we
    * can end up handling the X event before the wayland requests
    * and thus when we try to look up the surface ID, the surface
    * hasn't been created yet. */
   struct wlc_x11_window *win, *wn;
   wl_list_for_each_safe(win, wn, &xwm.unpaired_windows, link) {
      if (!win->surface_id)
         continue;

      link_surface(win, wl_client_get_object(xwm.client, win->surface_id));
   }
}

bool
wlc_xwm_init(struct wlc_compositor *compositor, struct wl_client *client, const int fd)
{
   if (!xcb_load() || !xcb_composite_load() || !xcb_xfixes_load())
      goto fail;

   x11.connection = x11.api.xcb_connect_to_fd(fd, NULL);
   if (x11.api.xcb_connection_has_error(x11.connection))
      goto xcb_connection_fail;

   x11.api.xcb_prefetch_extension_data(x11.connection, x11.api.xcb_composite_id);

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
   for (int i = 0; i < ATOM_LAST; ++i)
      atom_cookies[map[i].atom] = x11.api.xcb_intern_atom(x11.connection, 0, strlen(map[i].name), map[i].name);

   const xcb_setup_t *setup = x11.api.xcb_get_setup(x11.connection);
   xcb_screen_iterator_t screen_iterator = x11.api.xcb_setup_roots_iterator(setup);
   x11.screen = screen_iterator.data;

   uint32_t value = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_PROPERTY_CHANGE;
   if (!XCB_CALL(x11.api.xcb_change_window_attributes_checked(x11.connection, x11.screen->root, XCB_CW_EVENT_MASK, &value)))
      goto change_attributes_fail;

   if (!(xwm.event_source = wl_event_loop_add_fd(compositor->event_loop, fd, WL_EVENT_READABLE, &x11_event, compositor)))
      goto event_source_fail;

   wl_list_init(&xwm.windows);
   wl_list_init(&xwm.unpaired_windows);

   if (!(x11.xfixes = x11.api.xcb_get_extension_data(x11.connection, x11.api.xcb_xfixes_id)) || !x11.xfixes->present)
      goto xfixes_extension_fail;

   xcb_xfixes_query_version_reply_t *xfixes_reply;
   if (!(xfixes_reply = x11.api.xcb_xfixes_query_version_reply(x11.connection, x11.api.xcb_xfixes_query_version(x11.connection, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION), NULL)))
      goto xfixes_extension_fail;

   wlc_log(WLC_LOG_INFO, "xfixes (%d.%d)", xfixes_reply->major_version, xfixes_reply->minor_version);
   free(xfixes_reply);

   const xcb_query_extension_reply_t *composite_extension;
   if (!(composite_extension = x11.api.xcb_get_extension_data(x11.connection, x11.api.xcb_composite_id)) || !composite_extension->present)
      goto composite_extension_fail;

   if (!XCB_CALL(x11.api.xcb_composite_redirect_subwindows_checked(x11.connection, x11.screen->root, XCB_COMPOSITE_REDIRECT_MANUAL)))
      goto redirect_subwindows_fail;

   for (int i = 0; i < ATOM_LAST; ++i) {
      xcb_generic_error_t *error;
      xcb_intern_atom_reply_t *atom_reply = x11.api.xcb_intern_atom_reply(x11.connection, atom_cookies[map[i].atom], &error);

      if (atom_reply && !error)
         x11.atoms[map[i].atom] = atom_reply->atom;

      if (atom_reply)
         free(atom_reply);

      if (error)
         goto atom_get_fail;
   }

   if (!(x11.window = x11.api.xcb_generate_id(x11.connection)))
      goto window_fail;

   XCB_CALL(x11.api.xcb_create_window_checked(
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

   XCB_CALL(x11.api.xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, x11.screen->root, x11.atoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, LENGTH(supported), supported));
   XCB_CALL(x11.api.xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, x11.screen->root, x11.atoms[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &x11.window));
   XCB_CALL(x11.api.xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, x11.window, x11.atoms[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &x11.window));
   XCB_CALL(x11.api.xcb_change_property_checked(x11.connection, XCB_PROP_MODE_REPLACE, x11.window, x11.atoms[NET_WM_NAME], x11.atoms[UTF8_STRING], 8, strlen("xwlc"), "xwlc"));
   XCB_CALL(x11.api.xcb_set_selection_owner_checked(x11.connection, x11.window, x11.atoms[CLIPBOARD_MANAGER], XCB_CURRENT_TIME));
   XCB_CALL(x11.api.xcb_set_selection_owner_checked(x11.connection, x11.window, x11.atoms[WM_S0], XCB_CURRENT_TIME));
   XCB_CALL(x11.api.xcb_set_selection_owner_checked(x11.connection, x11.window, x11.atoms[NET_WM_S0], XCB_CURRENT_TIME));

   uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
   XCB_CALL(x11.api.xcb_xfixes_select_selection_input_checked(x11.connection, x11.window, x11.atoms[CLIPBOARD], mask));

   x11.api.xcb_flush(x11.connection);

   xwm.client = client;
   xwm.compositor = compositor;
   wlc_log(WLC_LOG_INFO, "xwm started");
   return true;

xcb_connection_fail:
   wlc_log(WLC_LOG_WARN, "Failed to connect to Xwayland");
   goto fail;
event_source_fail:
   wlc_log(WLC_LOG_WARN, "Failed to setup X11 event source");
   goto fail;
xfixes_extension_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get xfixes extension");
   goto fail;
composite_extension_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get composite extension");
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
   goto fail;
fail:
   wlc_xwm_deinit();
   return false;
}

void
wlc_xwm_deinit()
{
   if (xwm.event_source)
      wl_event_source_remove(xwm.event_source);

   if (x11.window)
      XCB_CALL(x11.api.xcb_destroy_window_checked(x11.connection, x11.window));

   if (x11.connection)
      x11.api.xcb_disconnect(x11.connection);

   if (x11.api.xcb_handle)
      dlclose(x11.api.xcb_handle);

   memset(&xwm, 0, sizeof(xwm));
   memset(&x11, 0, sizeof(x11));
}
