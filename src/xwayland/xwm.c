/* This is mostly based on swc's xwm.c */

#include "xwm.h"
#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "compositor/view.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <xcb/composite.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>

#include <wayland-server.h>
#include <wayland-util.h>

enum atom_name {
   WL_SURFACE_ID,
   WM_DELETE_WINDOW,
   WM_PROTOCOLS,
   WM_S0,
   ATOM_LAST
};

struct wlc_x11_window {
   struct wlc_surface *surface;
   struct wl_list link;
   xcb_window_t id;
   bool override_redirect;
};

static struct {
   struct wl_event_source *event_source;
   xcb_screen_t *screen;
   xcb_connection_t *connection;
   xcb_atom_t atoms[ATOM_LAST];
   xcb_ewmh_connection_t ewmh;
   xcb_window_t window;
   struct wl_list windows, unpaired_windows;
} xwm;

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

static void
wlc_x11_window_free(struct wlc_x11_window *win)
{
   assert(win);
   wl_list_remove(&win->link);
   free(win);
}

void
wlc_x11_window_resize(struct wlc_x11_window *win, const uint32_t width, const uint32_t height)
{
   static const uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
   const uint32_t values[] = { width, height };
   xcb_configure_window(xwm.connection, win->id, mask, (uint32_t*)&values);
   xcb_flush(xwm.connection);
}

static void
link_surface(struct wlc_compositor *compositor, struct wlc_x11_window *win, const uint32_t surface_id)
{
   struct wlc_view *view;
   if (!(view = wlc_view_for_surface_id_in_list(surface_id, &compositor->views)))
      return;

   win->surface = view->surface;
   view->x11_window = win;

   if (!view->surface->created && compositor->interface.view.created) {
      view->geometry.w = view->surface->width;
      view->geometry.h = view->surface->height;
      compositor->interface.view.created(compositor, view);
      view->surface->created = true;
   }

   wlc_x11_window_resize(win, view->geometry.w, view->geometry.h);
   xcb_flush(xwm.connection);

   wl_list_insert(&xwm.windows, &win->link);
}

static int
x11_event(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;
   struct wlc_compositor *compositor = data;

   int count = 0;
   xcb_generic_event_t *event;
   while ((event = xcb_poll_for_event(xwm.connection))) {
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
            xcb_map_window(xwm.connection, ev->window);
         }
         break;
         case XCB_CONFIGURE_REQUEST:
         break;
         case XCB_PROPERTY_NOTIFY:
         break;
         case XCB_CLIENT_MESSAGE: {
            xcb_client_message_event_t *ev = (xcb_client_message_event_t*)event;
            if (ev->type == xwm.atoms[WL_SURFACE_ID]) {
               struct wlc_x11_window *win;
               if ((win = wlc_x11_window_for_id(&xwm.unpaired_windows, ev->window)))
                  link_surface(compositor, win, ev->data.data32[0]);
            }
         }
         break;
      }

      free(event);
      count += 1;
   }

   xcb_flush(xwm.connection);
   return count;
}

bool
wlc_xwm_init(struct wlc_compositor *compositor, const int fd)
{
   xwm.connection = xcb_connect_to_fd(fd, NULL);
   if (xcb_connection_has_error(xwm.connection))
      goto xcb_connection_fail;

   xcb_prefetch_extension_data(xwm.connection, &xcb_composite_id);

   xcb_intern_atom_cookie_t *ewmh_cookies;
   if (!(ewmh_cookies = xcb_ewmh_init_atoms(xwm.connection, &xwm.ewmh)))
      goto emwh_init_atoms_fail;

   struct {
      const char *name;
      enum atom_name atom;
   } map[ATOM_LAST] = {
      { "WL_SURFACE_ID", WL_SURFACE_ID },
      { "WM_PROTOCOLS", WM_PROTOCOLS },
      { "WM_DELETE_WINDOW", WM_DELETE_WINDOW },
      { "WM_S0", WM_S0 },
   };

   xcb_intern_atom_cookie_t atom_cookies[ATOM_LAST];
   for (int i = 0; i < ATOM_LAST; ++i)
      atom_cookies[map[i].atom] = xcb_intern_atom(xwm.connection, 0, strlen(map[i].name), map[i].name);

   const xcb_setup_t *setup = xcb_get_setup(xwm.connection);
   xcb_screen_iterator_t screen_iterator = xcb_setup_roots_iterator(setup);
   xwm.screen = screen_iterator.data;

   /* Try to select for substructure redirect. */
   unsigned int mask = XCB_CW_EVENT_MASK;
   unsigned int value = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
   xcb_void_cookie_t change_attributes_cookie = xcb_change_window_attributes(xwm.connection, xwm.screen->root, mask, &value);

   if (!(xwm.event_source = wl_event_loop_add_fd(compositor->event_loop, fd, WL_EVENT_READABLE, &x11_event, compositor)))
      goto event_source_fail;

   wl_list_init(&xwm.windows);
   wl_list_init(&xwm.unpaired_windows);

   const xcb_query_extension_reply_t *composite_extension;
   if (!(composite_extension = xcb_get_extension_data(xwm.connection, &xcb_composite_id)) || !composite_extension->present)
      goto composite_extension_fail;

   xcb_void_cookie_t redirect_subwindows_cookie = xcb_composite_redirect_subwindows_checked(xwm.connection, xwm.screen->root, XCB_COMPOSITE_REDIRECT_MANUAL);

   xcb_generic_error_t *error;
   if ((error = xcb_request_check(xwm.connection, change_attributes_cookie)))
      goto change_attributes_fail;

   if ((error = xcb_request_check(xwm.connection, redirect_subwindows_cookie)))
      goto redirect_subwindows_fail;

   if (!(xwm.window = xcb_generate_id(xwm.connection)))
      goto window_fail;

   xcb_create_window(xwm.connection, 0, xwm.window, xwm.screen->root,
         0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
         XCB_COPY_FROM_PARENT, 0, NULL);

   xcb_ewmh_init_atoms_replies(&xwm.ewmh, ewmh_cookies, &error);

   if (error)
      goto emwh_init_atom_replies_fail;

   for (int i = 0; i < ATOM_LAST; ++i) {
      xcb_intern_atom_reply_t *atom_reply = xcb_intern_atom_reply(xwm.connection, atom_cookies[map[i].atom], &error);

      if (atom_reply && !error)
         xwm.atoms[map[i].atom] = atom_reply->atom;

      if (atom_reply)
         free(atom_reply);

      if (error)
         goto atom_get_fail;
   }

   xcb_set_selection_owner(xwm.connection, xwm.window, xwm.atoms[WM_S0], XCB_CURRENT_TIME);
   xcb_flush(xwm.connection);

   fprintf(stdout, "-!- xwm started\n");
   return true;

   // TODO: error handling
xcb_connection_fail:
emwh_init_atoms_fail:
event_source_fail:
composite_extension_fail:
change_attributes_fail:
redirect_subwindows_fail:
window_fail:
emwh_init_atom_replies_fail:
atom_get_fail:
fail:
   xcb_ewmh_connection_wipe(&xwm.ewmh);

   if (xwm.connection)
      xcb_disconnect(xwm.connection);

   return false;
}

void
wlc_xwm_deinit()
{
   if (xwm.event_source)
      wl_event_source_remove(xwm.event_source);

   xcb_ewmh_connection_wipe(&xwm.ewmh);

   if (xwm.connection)
      xcb_disconnect(xwm.connection);

   memset(&xwm, 0, sizeof(xwm));
}
