#include "x11.h"

#include "seat/seat.h"

#include <xcb/xcb.h>
#include <X11/Xlib-xcb.h>
#include <linux/input.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static struct {
   Display *display;
   xcb_connection_t *connection;
   xcb_screen_t *screen;
   xcb_window_t window;
   xcb_cursor_t cursor;

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
      xcb_generic_error_t* (*xcb_request_check)(xcb_connection_t*, xcb_void_cookie_t);
      xcb_generic_event_t* (*xcb_poll_for_event)(xcb_connection_t*);
      int (*xcb_get_file_descriptor)(xcb_connection_t*);
   } api;
} x11;

static bool
x11_load(void)
{
   const char *lib = "libX11.so", *func = NULL;

   if (!(x11.api.x11_handle = dlopen(lib, RTLD_LAZY)))
      return false;

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

   if (!(x11.api.x11_xcb_handle = dlopen(lib, RTLD_LAZY)))
      return false;

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

   if (!(x11.api.xcb_handle = dlopen(lib, RTLD_LAZY)))
      return false;

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

Display*
wlc_x11_display(void)
{
   return x11.display;
}

Window
wlc_x11_window(void)
{
   return x11.window;
}

int
wlc_x11_event_fd(void)
{
   return x11.api.xcb_get_file_descriptor(x11.connection);
}

int
wlc_x11_poll_events(struct wlc_seat *seat)
{
   int count = 0;
   xcb_generic_event_t *event;
   while ((event = x11.api.xcb_poll_for_event(x11.connection))) {
      switch (event->response_type & ~0x80) {
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
      }
      count += 1;
   }
   return count;
}

void
wlc_x11_terminate(void)
{
   if (x11.cursor)
      x11.api.xcb_free_cursor(x11.connection, x11.cursor);

   if (x11.window && x11.window != x11.screen->root)
      x11.api.xcb_destroy_window(x11.connection, x11.window);

   if (x11.display)
      x11.api.XCloseDisplay(x11.display);

   if (x11.api.x11_handle)
      dlclose(x11.api.x11_handle);

   if (x11.api.x11_xcb_handle)
      dlclose(x11.api.x11_xcb_handle);

   if (x11.api.xcb_handle)
      dlclose(x11.api.xcb_handle);

   memset(&x11, 0, sizeof(x11));
}

bool
wlc_x11_init(void)
{
   if (!x11_load()) {
      wlc_x11_terminate();
      return false;
   }

   if (!x11_xcb_load()) {
      wlc_x11_terminate();
      return false;
   }

   if (!xcb_load()) {
      wlc_x11_terminate();
      return false;
   }

   if (!(x11.display = x11.api.XOpenDisplay(NULL)))
      goto display_open_fail;

   if (!(x11.connection = x11.api.XGetXCBConnection(x11.display)))
      goto xcb_connection_fail;

   x11.api.XSetEventQueueOwner(x11.display, XCBOwnsEventQueue);

   if (x11.api.xcb_connection_has_error(x11.connection))
      goto xcb_connection_fail;

   xcb_screen_iterator_t s = x11.api.xcb_setup_roots_iterator(x11.api.xcb_get_setup(x11.connection));
   x11.screen = s.data;
   int width = x11.screen->width_in_pixels;
   int height = x11.screen->height_in_pixels;
   width = 800;
   height = 480;

   xcb_gc_t gc = x11.api.xcb_generate_id(x11.connection);
   xcb_pixmap_t pixmap = x11.api.xcb_generate_id(x11.connection);

   if (!(x11.cursor = x11.api.xcb_generate_id(x11.connection)))
      goto cursor_fail;

#if 0
   uint8_t data[] = { 0, 0, 0, 0 };
   x11.api.xcb_create_pixmap(x11.connection, 1, pixmap, x11.screen->root, 1, 1);
   x11.api.xcb_create_gc(x11.connection, gc, pixmap, 0, NULL);
   x11.api.xcb_put_image(x11.connection, XCB_IMAGE_FORMAT_XY_PIXMAP, pixmap, gc, 1, 1, 0, 0, 0, 32, sizeof(data), data);
   x11.api.xcb_create_cursor(x11.connection, x11.cursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 1, 1);
   x11.api.xcb_free_gc(x11.connection, gc);
   x11.api.xcb_free_pixmap(x11.connection, pixmap);
#endif

   uint32_t mask = XCB_CW_EVENT_MASK; // | XCB_CW_CURSOR;
   uint32_t values[] = {
      XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
      x11.cursor,
   };

   xcb_window_t window;
   if (!(window = x11.api.xcb_generate_id(x11.connection)))
      goto window_fail;

   xcb_void_cookie_t create_cookie = x11.api.xcb_create_window_checked(x11.connection, XCB_COPY_FROM_PARENT, window, x11.screen->root, 0, 0, width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual, mask, values);
   xcb_void_cookie_t map_cookie = x11.api.xcb_map_window_checked(x11.connection, window);

   xcb_generic_error_t *error;
   if ((error = x11.api.xcb_request_check(x11.connection, create_cookie)) || (error = x11.api.xcb_request_check(x11.connection, map_cookie)))
      goto window_fail;

   /* set this to root to run as x11 "wm"
    * TODO: check atom for wm and if it doesn't exist, set as root and skip window creation. */
   x11.window = window;
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
fail:
   wlc_x11_terminate();
   return false;
}
