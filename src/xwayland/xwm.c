#include "wlc.h"
#include "xwm.h"

#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "compositor/output.h"
#include "compositor/view.h"

#include <string.h>
#include <assert.h>
#include <dlfcn.h>

#include <xcb/composite.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xfixes.h>

#include <wayland-server.h>
#include <wayland-util.h>

struct wlc_x11_window {
   struct wlc_view *view;
   struct wl_list link;
   xcb_window_t id;
   uint32_t surface_id;
   bool override_redirect;
};

enum atom_name {
   WL_SURFACE_ID,
   WM_DELETE_WINDOW,
   WM_TAKE_FOCUS,
   WM_PROTOCOLS,
   UTF8_STRING,
   CLIPBOARD,
   CLIPBOARD_MANAGER,
   WM_S0,
   NET_WM_S0,
   NET_WM_NAME,
   NET_WM_STATE,
   NET_WM_STATE_FULLSCREEN,
   NET_SUPPORTED,
   NET_SUPPORTING_WM_CHECK,
   ATOM_LAST
};

static struct {
   struct wl_client *client;
   struct wl_event_source *event_source;
   struct wl_list windows, unpaired_windows;
} xwm;

static struct {
   xcb_screen_t *screen;
   xcb_connection_t *connection;
   const xcb_query_extension_reply_t *xfixes;
   xcb_atom_t atoms[ATOM_LAST];
   xcb_ewmh_connection_t ewmh;
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
      xcb_void_cookie_t (*xcb_create_window_checked)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_window_t, int16_t, int16_t, uint16_t, uint16_t, uint16_t, uint16_t, xcb_visualid_t, uint32_t, const uint32_t*);
      xcb_void_cookie_t (*xcb_destroy_window)(xcb_connection_t*, xcb_window_t);
      xcb_void_cookie_t (*xcb_map_window_checked)(xcb_connection_t*, xcb_window_t);
      xcb_void_cookie_t (*xcb_change_property)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint8_t, uint32_t, const void*);
      xcb_void_cookie_t (*xcb_change_window_attributes_checked)(xcb_connection_t*, xcb_window_t, uint32_t, const uint32_t*);
      xcb_void_cookie_t (*xcb_configure_window_checked)(xcb_connection_t*, xcb_window_t, uint16_t, const uint32_t*);
      xcb_void_cookie_t (*xcb_set_selection_owner_checked)(xcb_connection_t*, xcb_window_t, xcb_atom_t, xcb_timestamp_t);
      xcb_void_cookie_t (*xcb_set_input_focus_checked)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_timestamp_t);
      xcb_void_cookie_t (*xcb_kill_client_checked)(xcb_connection_t*, uint32_t);
      xcb_void_cookie_t (*xcb_send_event)(xcb_connection_t*, uint8_t, xcb_window_t, uint32_t, const char*);
      xcb_intern_atom_cookie_t (*xcb_intern_atom)(xcb_connection_t*, uint8_t, uint16_t, const char*);
      xcb_intern_atom_reply_t* (*xcb_intern_atom_reply)(xcb_connection_t*, xcb_intern_atom_cookie_t, xcb_generic_error_t**);
      xcb_generic_error_t* (*xcb_request_check)(xcb_connection_t*, xcb_void_cookie_t);
      xcb_generic_event_t* (*xcb_poll_for_event)(xcb_connection_t*);
      xcb_query_extension_reply_t* (*xcb_get_extension_data)(xcb_connection_t*, xcb_extension_t*);

      xcb_intern_atom_cookie_t* (*xcb_ewmh_init_atoms)(xcb_connection_t*, xcb_ewmh_connection_t*);
      uint8_t (*xcb_ewmh_init_atoms_replies)(xcb_ewmh_connection_t*, xcb_intern_atom_cookie_t*, xcb_generic_error_t**);

      xcb_void_cookie_t (*xcb_composite_redirect_subwindows_checked)(xcb_connection_t*, xcb_window_t, uint8_t);
      xcb_extension_t *xcb_composite_id;

      xcb_get_property_cookie_t (*xcb_icccm_get_wm_protocols)(xcb_connection_t*, xcb_window_t, xcb_atom_t);
      uint8_t (*xcb_icccm_get_wm_protocols_reply)(xcb_connection_t*, xcb_get_property_cookie_t, xcb_icccm_get_wm_protocols_reply_t*, xcb_generic_error_t**);
      void (*xcb_icccm_get_wm_protocols_reply_wipe)(xcb_icccm_get_wm_protocols_reply_t*);

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
   if (!load(xcb_create_window_checked))
      goto function_pointer_exception;
   if (!load(xcb_destroy_window))
      goto function_pointer_exception;
   if (!load(xcb_map_window_checked))
      goto function_pointer_exception;
   if (!load(xcb_change_property))
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
   if (!load(xcb_send_event))
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
xcb_ewmh_load(void)
{
   const char *lib = "libxcb-ewmh.so", *func = NULL;

   if (!(x11.api.xcb_handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (x11.api.x = dlsym(x11.api.xcb_handle, (func = #x)))

   if (!load(xcb_ewmh_init_atoms))
      goto function_pointer_exception;
   if (!load(xcb_ewmh_init_atoms_replies))
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
xcb_icccm_load(void)
{
   const char *lib = "libxcb-icccm.so", *func = NULL;

   if (!(x11.api.xcb_handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (x11.api.x = dlsym(x11.api.xcb_handle, (func = #x)))

   if (!load(xcb_icccm_get_wm_protocols))
      goto function_pointer_exception;
   if (!load(xcb_icccm_get_wm_protocols_reply))
      goto function_pointer_exception;
   if (!load(xcb_icccm_get_wm_protocols_reply_wipe))
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

   if (win->view) {
      win->view->x11_window = NULL;
      win->view->client = NULL;
   }

   wl_list_remove(&win->link);
   free(win);
}

static void deletewindow(const xcb_window_t window)
{
    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = window;
    ev.format = 32;
    ev.sequence = 0;
    ev.type = x11.atoms[WM_PROTOCOLS];
    ev.data.data32[0] = x11.atoms[WM_DELETE_WINDOW];
    ev.data.data32[1] = XCB_CURRENT_TIME;
    x11.api.xcb_send_event(x11.connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
}

void
wlc_x11_window_close(struct wlc_x11_window *win)
{
   bool got = false;
   xcb_icccm_get_wm_protocols_reply_t reply;
   if (x11.api.xcb_icccm_get_wm_protocols_reply(x11.connection, x11.api.xcb_icccm_get_wm_protocols(x11.connection, win->id, x11.atoms[WM_PROTOCOLS]), &reply, NULL)) {
      for(unsigned int n = 0; n != reply.atoms_len; ++n) if ((got = reply.atoms[n] == x11.atoms[WM_DELETE_WINDOW])) break;
      x11.api.xcb_icccm_get_wm_protocols_reply_wipe(&reply);
   }

   if (got) {
      deletewindow(win->id);
   } else {
      x11.api.xcb_kill_client_checked(x11.connection, win->id);
   }

   x11.api.xcb_flush(x11.connection);
}

void
wlc_x11_window_position(struct wlc_x11_window *win, const int32_t x, const int32_t y)
{
   assert(win);
   static const uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
   const uint32_t values[] = { x, y };
   x11.api.xcb_configure_window_checked(x11.connection, win->id, mask, (uint32_t*)&values);
   x11.api.xcb_flush(x11.connection);
}

void
wlc_x11_window_resize(struct wlc_x11_window *win, const uint32_t width, const uint32_t height)
{
   assert(win);
   static const uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
   const uint32_t values[] = { width, height };
   x11.api.xcb_configure_window_checked(x11.connection, win->id, mask, (uint32_t*)&values);
   x11.api.xcb_flush(x11.connection);
}

void
wlc_x11_window_set_active(struct wlc_x11_window *win, bool active)
{
   assert(win);

   if (active) {
      xcb_client_message_event_t m;
      m.response_type = XCB_CLIENT_MESSAGE;
      m.format = 32;
      m.window = win->id;
      m.type = x11.atoms[WM_PROTOCOLS];
      m.data.data32[0] = x11.atoms[WM_TAKE_FOCUS];
      m.data.data32[1] = XCB_TIME_CURRENT_TIME;
      x11.api.xcb_send_event(x11.connection, 0, win->id, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char*)&m);
      x11.api.xcb_set_input_focus_checked(x11.connection, XCB_INPUT_FOCUS_POINTER_ROOT, win->id, XCB_CURRENT_TIME);
   } else {
      x11.api.xcb_set_input_focus_checked(x11.connection, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_NONE, XCB_CURRENT_TIME);
   }

   x11.api.xcb_flush(x11.connection);
}

static void
link_surface(struct wlc_x11_window *win, struct wl_resource *resource)
{
   if (!resource || win->surface_id != 0)
      return;

   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   struct wlc_view *view;
   if (!(view = wlc_view_for_surface_in_list(surface, &surface->compositor->unmapped)))
      return;

   win->view = view;
   view->x11_window = win;

   wlc_x11_window_resize(win, view->geometry.w, view->geometry.h);
   wlc_surface_create_notify(view->surface);

   wl_list_remove(&win->link);
   wl_list_insert(&xwm.windows, &win->link);
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
            case XCB_CREATE_NOTIFY: {
               xcb_create_notify_event_t *ev = (xcb_create_notify_event_t*)event;
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
               x11.api.xcb_map_window_checked(x11.connection, ev->window);
            }
            break;
            case XCB_CLIENT_MESSAGE: {
               xcb_client_message_event_t *ev = (xcb_client_message_event_t*)event;
               if (ev->type == x11.atoms[WL_SURFACE_ID]) {
                  struct wlc_x11_window *win;
                  if ((win = wlc_x11_window_for_id(&xwm.unpaired_windows, ev->window))) {
                     link_surface(win, wl_client_get_object(xwm.client, ev->data.data32[0]));
                     win->surface_id = ev->data.data32[0];
                  }
               }
            }
            break;
            case XCB_SELECTION_NOTIFY:
            break;
            case XCB_PROPERTY_NOTIFY:
            break;
            case XCB_SELECTION_REQUEST:
            break;
            case XCB_CONFIGURE_REQUEST:
            case XCB_CONFIGURE_NOTIFY:
            case XCB_MAP_NOTIFY:
            case XCB_UNMAP_NOTIFY:
            case XCB_MAPPING_NOTIFY:
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

bool
wlc_xwm_init(struct wlc_compositor *compositor, struct wl_client *client, const int fd)
{
   if (!xcb_load() || !xcb_ewmh_load() || !xcb_composite_load() || !xcb_icccm_load() || !xcb_xfixes_load())
      goto fail;

   x11.connection = x11.api.xcb_connect_to_fd(fd, NULL);
   if (x11.api.xcb_connection_has_error(x11.connection))
      goto xcb_connection_fail;

   x11.api.xcb_prefetch_extension_data(x11.connection, x11.api.xcb_composite_id);

   xcb_intern_atom_cookie_t *ewmh_cookies;
   if (!(ewmh_cookies = x11.api.xcb_ewmh_init_atoms(x11.connection, &x11.ewmh)))
      goto emwh_init_atoms_fail;

   struct {
      const char *name;
      enum atom_name atom;
   } map[ATOM_LAST] = {
      { "WL_SURFACE_ID", WL_SURFACE_ID },
      { "WM_DELETE_WINDOW", WM_DELETE_WINDOW },
      { "WM_TAKE_FOCUS", WM_TAKE_FOCUS },
      { "WM_PROTOCOLS", WM_PROTOCOLS },
      { "UTF8_STRING", UTF8_STRING },
      { "CLIPBOARD", CLIPBOARD },
      { "CLIPBOARD_MANAGER", CLIPBOARD_MANAGER },
      { "WM_S0", WM_S0 },
      { "_NET_WM_S0", NET_WM_S0 },
      { "_NET_WM_NAME", NET_WM_NAME },
      { "_NET_WM_STATE", NET_WM_STATE },
      { "_NET_WM_STATE_FULLSCREEN", NET_WM_STATE_FULLSCREEN },
      { "_NET_SUPPORTED", NET_SUPPORTED },
      { "_NET_SUPPORTING_WM_CHECK", NET_SUPPORTING_WM_CHECK },
   };

   xcb_intern_atom_cookie_t atom_cookies[ATOM_LAST];
   for (int i = 0; i < ATOM_LAST; ++i)
      atom_cookies[map[i].atom] = x11.api.xcb_intern_atom(x11.connection, 0, strlen(map[i].name), map[i].name);

   const xcb_setup_t *setup = x11.api.xcb_get_setup(x11.connection);
   xcb_screen_iterator_t screen_iterator = x11.api.xcb_setup_roots_iterator(setup);
   x11.screen = screen_iterator.data;

   uint32_t value = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_PROPERTY_CHANGE;
   xcb_void_cookie_t change_attributes_cookie = x11.api.xcb_change_window_attributes_checked(x11.connection, x11.screen->root, XCB_CW_EVENT_MASK, &value);

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

   const xcb_query_extension_reply_t *composite_extension;
   if (!(composite_extension = x11.api.xcb_get_extension_data(x11.connection, x11.api.xcb_composite_id)) || !composite_extension->present)
      goto composite_extension_fail;

   xcb_void_cookie_t redirect_subwindows_cookie = x11.api.xcb_composite_redirect_subwindows_checked(x11.connection, x11.screen->root, XCB_COMPOSITE_REDIRECT_MANUAL);

   xcb_generic_error_t *error;
   if ((error = x11.api.xcb_request_check(x11.connection, change_attributes_cookie)))
      goto change_attributes_fail;

   if ((error = x11.api.xcb_request_check(x11.connection, redirect_subwindows_cookie)))
      goto redirect_subwindows_fail;

   x11.api.xcb_ewmh_init_atoms_replies(&x11.ewmh, ewmh_cookies, &error);

   if (error)
      goto emwh_init_atom_replies_fail;

   for (int i = 0; i < ATOM_LAST; ++i) {
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

   x11.api.xcb_create_window_checked(x11.connection, XCB_COPY_FROM_PARENT, x11.window, x11.screen->root,
         0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
         x11.screen->root_visual, XCB_CW_EVENT_MASK, (uint32_t[]){XCB_EVENT_MASK_PROPERTY_CHANGE});

   xcb_atom_t supported[] = {
      x11.atoms[NET_WM_STATE],
      x11.atoms[NET_WM_STATE_FULLSCREEN]
   };

   x11.api.xcb_change_property(x11.connection, XCB_PROP_MODE_REPLACE, x11.screen->root, x11.atoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, 2, supported);
   x11.api.xcb_change_property(x11.connection, XCB_PROP_MODE_REPLACE, x11.screen->root, x11.atoms[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &x11.window);
   x11.api.xcb_change_property(x11.connection, XCB_PROP_MODE_REPLACE, x11.window, x11.atoms[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1, &x11.window);
   x11.api.xcb_change_property(x11.connection, XCB_PROP_MODE_REPLACE, x11.window, x11.atoms[NET_WM_NAME], x11.atoms[UTF8_STRING], 8, strlen("xwlc"), "xwlc");
   x11.api.xcb_set_selection_owner_checked(x11.connection, x11.window, x11.atoms[CLIPBOARD_MANAGER], XCB_CURRENT_TIME);
   x11.api.xcb_set_selection_owner_checked(x11.connection, x11.window, x11.atoms[WM_S0], XCB_CURRENT_TIME);
   x11.api.xcb_set_selection_owner_checked(x11.connection, x11.window, x11.atoms[NET_WM_S0], XCB_CURRENT_TIME);

   uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
   x11.api.xcb_xfixes_select_selection_input_checked(x11.connection, x11.window, x11.atoms[CLIPBOARD], mask);

   x11.api.xcb_flush(x11.connection);

   xwm.client = client;
   wlc_log(WLC_LOG_INFO, "xwm started");
   return true;

xcb_connection_fail:
   wlc_log(WLC_LOG_WARN, "Failed to connect to Xwayland");
   goto fail;
emwh_init_atoms_fail:
   wlc_log(WLC_LOG_WARN, "Failed to init ewmh atoms");
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
emwh_init_atom_replies_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get replies for ewmh atoms");
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
      x11.api.xcb_destroy_window(x11.connection, x11.window);

   // inline in xcb_ewmh.h
   xcb_ewmh_connection_wipe(&x11.ewmh);

   if (x11.connection)
      x11.api.xcb_disconnect(x11.connection);

   if (x11.api.xcb_handle)
      dlclose(x11.api.xcb_handle);

   memset(&xwm, 0, sizeof(xwm));
   memset(&x11, 0, sizeof(x11));
}
