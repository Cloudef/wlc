#include "x11.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static struct {
   Display *display;
   Window window;
   Window root;
   int screen;

   struct {
      void *handle;
      Display* (*XOpenDisplay)(const char*);
      int (*XCloseDisplay)(Display*);
      Window (*XCreateWindow)(Display*, Window, int, int, unsigned int, unsigned int, unsigned int, int, unsigned int, Visual*, unsigned long, XSetWindowAttributes*);
      void (*XDestroyWindow)(Display*, Window);
      int (*XMapWindow)(Display*, Window);
      int (*XDisplayWidth)(Display*, int);
      int (*XDisplayHeight)(Display*, int);
   } api;
} x11;

static bool
x11_load(void)
{
   const char *lib = "libX11.so", *func = NULL;

   if (!(x11.api.handle = dlopen(lib, RTLD_LAZY)))
      return false;

#define load(x) (x11.api.x = dlsym(x11.api.handle, (func = #x)))

   if (!load(XOpenDisplay))
      goto function_pointer_exception;
   if (!load(XCloseDisplay))
      goto function_pointer_exception;
   if (!load(XCreateWindow))
      goto function_pointer_exception;
   if (!load(XDestroyWindow))
      goto function_pointer_exception;
   if (!load(XMapWindow))
      goto function_pointer_exception;
   if (!load(XDisplayWidth))
      goto function_pointer_exception;
   if (!load(XDisplayHeight))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   wlc_x11_terminate();
   return false;
}

Display*
wlc_x11_display(void)
{
   return x11.display;
}

int
wlc_x11_screen(void)
{
   return x11.screen;
}

Window
wlc_x11_window(void)
{
   return x11.window;
}

void
wlc_x11_terminate(void)
{
   if (x11.display)
      x11.api.XCloseDisplay(x11.display);

   if (x11.api.handle)
      dlclose(x11.api.handle);

   memset(&x11, 0, sizeof(x11));
}

bool
wlc_x11_init(void)
{
   if (!x11_load())
      return false;

   if (!(x11.display = x11.api.XOpenDisplay(NULL))) {
      fprintf(stderr, "-!- Failed to open X11 display");
      return false;
   }

   x11.screen = DefaultScreen(x11.display);
   x11.root = RootWindow(x11.display, x11.screen);
   int width = x11.api.XDisplayWidth(x11.display, x11.screen);
   int height = x11.api.XDisplayHeight(x11.display, x11.screen);

   width = 800;
   height = 480;

   XSetWindowAttributes wa;
   wa.override_redirect = (0 ? True : False);
   Window window = x11.api.XCreateWindow(x11.display, x11.root, 0, 0, width, height, 0, CopyFromParent, CopyFromParent, CopyFromParent, CWOverrideRedirect, &wa);
   x11.api.XMapWindow(x11.display, window);

   /* set this to root to run as x11 "wm"
    * TODO: check atom for wm and if it doesn't exist, set as root and skip window creation. */
   x11.window = window;
   return true;
}
