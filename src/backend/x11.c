#include "x11.h"
#include "backend.h"

#include "seat/seat.h"
#include "compositor/compositor.h"
#include "compositor/output.h"

#include <xcb/xcb.h>
#include <X11/Xlib-xcb.h>
#include <linux/input.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <wayland-server.h>

#define X11_USE_UDEV_LIBINPUT 0
#if X11_USE_UDEV_LIBINPUT
#  include "udev/udev.h"
#endif

enum atom_name {
   WM_PROTOCOLS,
   WM_DELETE_WINDOW,
   WM_CLASS,
   NET_WM_NAME,
   STRING,
   UTF8_STRING,
   ATOM_LAST
};

static struct {
   Display *display;
   xcb_connection_t *connection;
   xcb_screen_t *screen;
   xcb_window_t window;
   xcb_cursor_t cursor;
   xcb_atom_t atoms[ATOM_LAST];

   struct {
      void *x11_handle;
      void *x11_xcb_handle;
      void *xcb_handle;

      Display* (*XOpenDisplay)(const char*);
      int (*XCloseDisplay)(Display*);

      xcb_connection_t* (*XGetXCBConnection)(Display*);
      void (*XSetEventQueueOwner)(Display*, enum XEventQueueOwner);

      int (*xcb_connection_has_error)(xcb_connection_t*);
      const xcb_setup_t* (*xcb_get_setup)(xcb_connection_t*);
      xcb_screen_iterator_t (*xcb_setup_roots_iterator)(const xcb_setup_t*);
      uint32_t (*xcb_generate_id)(xcb_connection_t*);
      xcb_void_cookie_t (*xcb_create_window_checked)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_window_t, int16_t, int16_t, uint16_t, uint16_t, uint16_t, uint16_t, xcb_visualid_t, uint32_t, const uint32_t*);
      xcb_void_cookie_t (*xcb_destroy_window)(xcb_connection_t*, xcb_window_t);
      xcb_void_cookie_t (*xcb_map_window_checked)(xcb_connection_t*, xcb_window_t);
      xcb_void_cookie_t (*xcb_create_pixmap)(xcb_connection_t*, uint8_t, xcb_pixmap_t, xcb_drawable_t, uint16_t, uint16_t);
      xcb_void_cookie_t (*xcb_create_gc)(xcb_connection_t*, xcb_gcontext_t, xcb_drawable_t, uint32_t, const uint32_t*);
      xcb_void_cookie_t (*xcb_free_pixmap)(xcb_connection_t*, xcb_pixmap_t pixmap);
      xcb_void_cookie_t (*xcb_free_gc)(xcb_connection_t*, xcb_gcontext_t);
      xcb_void_cookie_t (*xcb_put_image)(xcb_connection_t*, uint8_t, xcb_drawable_t, xcb_gcontext_t, uint16_t, uint16_t, int16_t, int16_t, uint8_t, uint8_t, uint32_t, const uint8_t*);
      xcb_void_cookie_t (*xcb_create_cursor)(xcb_connection_t*, xcb_cursor_t, xcb_pixmap_t, xcb_pixmap_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t);
      xcb_void_cookie_t (*xcb_free_cursor)(xcb_connection_t*, xcb_cursor_t);
      xcb_void_cookie_t (*xcb_change_property)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint8_t, uint32_t, const void*);
      xcb_void_cookie_t (*xcb_change_window_attributes_checked)(xcb_connection_t*, xcb_window_t, uint32_t, const uint32_t*);
      xcb_intern_atom_cookie_t (*xcb_intern_atom)(xcb_connection_t*, uint8_t, uint16_t, const char*);
      xcb_intern_atom_reply_t* (*xcb_intern_atom_reply)(xcb_connection_t*, xcb_intern_atom_cookie_t, xcb_generic_error_t**);
      xcb_generic_error_t* (*xcb_request_check)(xcb_connection_t*, xcb_void_cookie_t);
      xcb_generic_event_t* (*xcb_poll_for_event)(xcb_connection_t*);
      int (*xcb_get_file_descriptor)(xcb_connection_t*);
   } api;
} x11;

static struct {
#if X11_USE_UDEV_LIBINPUT
   struct wlc_udev *udev;
#endif
   struct wl_event_source *event_source;
} seat;

static bool
x11_load(void)
{
   const char *lib = "libX11.so", *func = NULL;

   if (!(x11.api.x11_handle = dlopen(lib, RTLD_LAZY))) {
      fprintf(stderr, "-!- %s\n", dlerror());
      return false;
   }

#define load(x) (x11.api.x = dlsym(x11.api.x11_handle, (func = #x)))

   if (!load(XOpenDisplay))
      goto function_pointer_exception;
   if (!load(XCloseDisplay))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   return false;
}

static bool
x11_xcb_load(void)
{
   const char *lib = "libX11-xcb.so", *func = NULL;

   if (!(x11.api.x11_xcb_handle = dlopen(lib, RTLD_LAZY))) {
      fprintf(stderr, "-!- %s\n", dlerror());
      return false;
   }

#define load(x) (x11.api.x = dlsym(x11.api.x11_xcb_handle, (func = #x)))

   if (!load(XGetXCBConnection))
      goto function_pointer_exception;
   if (!load(XSetEventQueueOwner))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   return false;
}

static bool
xcb_load(void)
{
   const char *lib = "libxcb.so", *func = NULL;

   if (!(x11.api.xcb_handle = dlopen(lib, RTLD_LAZY))) {
      fprintf(stderr, "-!- %s\n", dlerror());
      return false;
   }

#define load(x) (x11.api.x = dlsym(x11.api.xcb_handle, (func = #x)))

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
   if (!load(xcb_create_pixmap))
      goto function_pointer_exception;
   if (!load(xcb_create_gc))
      goto function_pointer_exception;
   if (!load(xcb_free_pixmap))
      goto function_pointer_exception;
   if (!load(xcb_free_gc))
      goto function_pointer_exception;
   if (!load(xcb_put_image))
      goto function_pointer_exception;
   if (!load(xcb_create_cursor))
      goto function_pointer_exception;
   if (!load(xcb_free_cursor))
      goto function_pointer_exception;
   if (!load(xcb_change_property))
      goto function_pointer_exception;
   if (!load(xcb_change_window_attributes_checked))
      goto function_pointer_exception;
   if (!load(xcb_intern_atom))
      goto function_pointer_exception;
   if (!load(xcb_intern_atom_reply))
      goto function_pointer_exception;
   if (!load(xcb_request_check))
      goto function_pointer_exception;
   if (!load(xcb_poll_for_event))
      goto function_pointer_exception;
   if (!load(xcb_get_file_descriptor))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   return false;
}

static EGLNativeDisplayType
get_display(void)
{
   return (EGLNativeDisplayType)x11.display;
}

static EGLNativeWindowType
get_window(void)
{
   return (EGLNativeWindowType)x11.window;
}

static int
x11_event(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;

   int count = 0;
   xcb_generic_event_t *event;
   struct wlc_seat *seat = data;

   while ((event = x11.api.xcb_poll_for_event(x11.connection))) {
      switch (event->response_type & ~0x80) {
         case XCB_CLIENT_MESSAGE: {
            xcb_client_message_event_t *ev = (xcb_client_message_event_t*)event;
            if (ev->data.data32[0] == x11.atoms[WM_DELETE_WINDOW])
               exit(0);
         }
         break;
         case XCB_EXPOSE:
            seat->compositor->api.schedule_repaint(seat->compositor);
            break;
         case XCB_CONFIGURE_NOTIFY: {
            xcb_configure_notify_event_t *ev = (xcb_configure_notify_event_t*)event;
            seat->compositor->api.resolution(seat->compositor, ev->width, ev->height);
         }
         break;
#if !X11_USE_UDEV_LIBINPUT
         case XCB_MOTION_NOTIFY: {
            xcb_motion_notify_event_t *ev = (xcb_motion_notify_event_t*)event;
            seat->notify.pointer_motion(seat, ev->event_x, ev->event_y);
         }
         break;
         case XCB_BUTTON_PRESS: {
            xcb_button_press_event_t *ev = (xcb_button_press_event_t*)event;
            uint32_t button = (ev->detail == 2 ? BTN_MIDDLE : (ev->detail == 3 ? BTN_RIGHT : ev->detail + BTN_LEFT - 1));
            seat->notify.pointer_button(seat, button, WL_POINTER_BUTTON_STATE_PRESSED);
         }
         break;
         case XCB_BUTTON_RELEASE: {
            xcb_button_press_event_t *ev = (xcb_button_press_event_t*)event;
            uint32_t button = (ev->detail == 2 ? BTN_MIDDLE : (ev->detail == 3 ? BTN_RIGHT : ev->detail + BTN_LEFT - 1));
            seat->notify.pointer_button(seat, button, WL_POINTER_BUTTON_STATE_RELEASED);
         }
         break;
         case XCB_KEY_PRESS: {
            xcb_key_press_event_t *ev = (xcb_key_press_event_t *)event;
            seat->notify.keyboard_key(seat, ev->detail - 8, WL_KEYBOARD_KEY_STATE_PRESSED);
         }
         break;
         case XCB_KEY_RELEASE: {
            xcb_key_press_event_t *ev = (xcb_key_release_event_t *)event;
            seat->notify.keyboard_key(seat, ev->detail - 8, WL_KEYBOARD_KEY_STATE_RELEASED);
         }
         break;
#endif
      }

      free(event);
      count += 1;
   }

   return count;
}

static void
terminate(void)
{
   if (x11.cursor)
      x11.api.xcb_free_cursor(x11.connection, x11.cursor);

   if (x11.window && x11.window != x11.screen->root)
      x11.api.xcb_destroy_window(x11.connection, x11.window);

   if (x11.display)
      x11.api.XCloseDisplay(x11.display);

   if (seat.event_source)
      wl_event_source_remove(seat.event_source);

   if (x11.api.x11_handle)
      dlclose(x11.api.x11_handle);

   if (x11.api.x11_xcb_handle)
      dlclose(x11.api.x11_xcb_handle);

   if (x11.api.xcb_handle)
      dlclose(x11.api.xcb_handle);

   memset(&x11, 0, sizeof(x11));
}

bool
wlc_x11_init(struct wlc_backend *out_backend, struct wlc_compositor *compositor)
{
   if (!x11_load())
      goto fail;

   if (!x11_xcb_load())
      goto fail;

   if (!xcb_load())
      goto fail;

   if (!(x11.display = x11.api.XOpenDisplay(NULL)))
      goto display_open_fail;

   if (!(x11.connection = x11.api.XGetXCBConnection(x11.display)))
      goto xcb_connection_fail;

   x11.api.XSetEventQueueOwner(x11.display, XCBOwnsEventQueue);

   if (x11.api.xcb_connection_has_error(x11.connection))
      goto xcb_connection_fail;

   struct {
      const char *name;
      enum atom_name atom;
   } map[ATOM_LAST] = {
      { "WM_PROTOCOLS", WM_PROTOCOLS },
      { "WM_DELETE_WINDOW", WM_DELETE_WINDOW },
      { "WM_CLASS", WM_CLASS },
      { "NET_WM_NAME", NET_WM_NAME },
      { "STRING", STRING },
      { "UTF8_STRING", UTF8_STRING },
   };

   for (int i = 0; i < ATOM_LAST; ++i) {
      xcb_intern_atom_reply_t *reply = x11.api.xcb_intern_atom_reply(x11.connection, x11.api.xcb_intern_atom(x11.connection, 0, strlen(map[i].name), map[i].name), 0);
      x11.atoms[map[i].atom] = (reply ? reply->atom : 0);
   }

   xcb_screen_iterator_t s = x11.api.xcb_setup_roots_iterator(x11.api.xcb_get_setup(x11.connection));
   x11.screen = s.data;

#if 0
   xcb_gc_t gc = x11.api.xcb_generate_id(x11.connection);
   xcb_pixmap_t pixmap = x11.api.xcb_generate_id(x11.connection);

   if (!(x11.cursor = x11.api.xcb_generate_id(x11.connection)))
      goto cursor_fail;

   uint8_t data[] = { 0, 0, 0, 0 };
   x11.api.xcb_create_pixmap(x11.connection, 1, pixmap, x11.screen->root, 1, 1);
   x11.api.xcb_create_gc(x11.connection, gc, pixmap, 0, NULL);
   x11.api.xcb_put_image(x11.connection, XCB_IMAGE_FORMAT_XY_PIXMAP, pixmap, gc, 1, 1, 0, 0, 0, 32, sizeof(data), data);
   x11.api.xcb_create_cursor(x11.connection, x11.cursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 1, 1);
   x11.api.xcb_free_gc(x11.connection, gc);
   x11.api.xcb_free_pixmap(x11.connection, pixmap);
#else
   if (0)
      goto cursor_fail;
#endif

   xcb_generic_error_t *error;
   unsigned int root_mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_BUTTON_PRESS;
   if ((error = x11.api.xcb_request_check(x11.connection, x11.api.xcb_change_window_attributes_checked(x11.connection, x11.screen->root, XCB_CW_EVENT_MASK, &root_mask)))) {
      uint32_t mask = XCB_CW_EVENT_MASK; // | XCB_CW_CURSOR;
      uint32_t values[] = {
            XCB_EVENT_MASK_EXPOSURE |
            XCB_EVENT_MASK_STRUCTURE_NOTIFY |
            XCB_EVENT_MASK_POINTER_MOTION |
            XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_KEY_PRESS |
            XCB_EVENT_MASK_KEY_RELEASE,
         x11.cursor,
      };

      xcb_window_t window;
      if (!(window = x11.api.xcb_generate_id(x11.connection)))
         goto window_fail;

      xcb_void_cookie_t create_cookie = x11.api.xcb_create_window_checked(x11.connection, XCB_COPY_FROM_PARENT, window, x11.screen->root, 0, 0, 800, 480, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual, mask, values);
      xcb_void_cookie_t map_cookie = x11.api.xcb_map_window_checked(x11.connection, window);

      if ((error = x11.api.xcb_request_check(x11.connection, create_cookie)) || (error = x11.api.xcb_request_check(x11.connection, map_cookie))) {
         free(error);
         goto window_fail;
      }

      x11.api.xcb_change_property(x11.connection, XCB_PROP_MODE_REPLACE, window, x11.atoms[WM_PROTOCOLS], XCB_ATOM_ATOM, 32, 1, &x11.atoms[WM_DELETE_WINDOW]);
      x11.api.xcb_change_property(x11.connection, XCB_PROP_MODE_REPLACE, window, x11.atoms[NET_WM_NAME], x11.atoms[UTF8_STRING], 8, 7, "wlc-x11");
      x11.api.xcb_change_property(x11.connection, XCB_PROP_MODE_REPLACE, window, x11.atoms[WM_CLASS], x11.atoms[STRING], 8, 7, "wlc-x11");
      x11.window = window;
   } else {
      x11.window = x11.screen->root;
   }

#if X11_USE_UDEV_LIBINPUT
   if (!(seat.udev = wlc_udev_new(compositor)))
      goto fail;
#endif

   if (!(seat.event_source = wl_event_loop_add_fd(compositor->event_loop, x11.api.xcb_get_file_descriptor(x11.connection), WL_EVENT_READABLE, x11_event, compositor->seat)))
      goto event_source_fail;

   wl_event_source_check(seat.event_source);

   struct wlc_output_information info;
   memset(&info, 0, sizeof(info));
   wlc_string_set(&info.make, "Xorg", false);
   wlc_string_set(&info.model, "X11 Window", false);

   struct wlc_output_mode mode;
   memset(&mode, 0, sizeof(mode));
   mode.refresh = 60;
   mode.width = x11.screen->width_in_pixels;
   mode.height = x11.screen->height_in_pixels;
   mode.flags = WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
   wlc_output_information_add_mode(&info, &mode);

   struct wlc_output *output;
   if (!(output = wlc_output_new(compositor, &info)))
      goto output_fail;

   compositor->api.add_output(compositor, output);

   out_backend->name = "X11";
   out_backend->terminate = terminate;
   out_backend->api.display = get_display;
   out_backend->api.window = get_window;
   return true;

display_open_fail:
   fprintf(stderr, "-!- Failed to open X11 display\n");
   goto fail;
xcb_connection_fail:
   fprintf(stderr, "-!- Failed to get xcb connection\n");
   goto fail;
cursor_fail:
   fprintf(stderr, "-!- Failed to create empty X11 cursor\n");
   goto fail;
window_fail:
   fprintf(stderr, "-!- Failed to create X11 window\n");
   goto fail;
event_source_fail:
   fprintf(stderr, "-!- Failed to add X11 event source\n");
   goto fail;
output_fail:
   fprintf(stderr, "-!- Failed to create output\n");
fail:
   terminate();
   return false;
}
