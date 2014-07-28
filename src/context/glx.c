#include "glx.h"
#include "x11.h"
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <assert.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

static struct {
   GLXContext context;
   const char *extensions;
   bool has_current;

   struct {
      void *handle;
      XVisualInfo* (*glXChooseVisual)(Display*, int, int*);
      GLXContext (*glXCreateContext)(Display*, XVisualInfo*, GLXContext, Bool);
      Bool (*glXDestroyContext)(Display*, GLXContext);
      Bool (*glXMakeCurrent)(Display*, GLXDrawable, GLXContext);
      void (*glXSwapBuffers)(Display*, GLXDrawable);
      const char* (*glXQueryExtensionsString)(Display*, int);
   } api;
} glx;

static bool
glx_load(void)
{
   const char *lib = "libGL.so", *func = NULL;

   if (!(glx.api.handle = dlopen(lib, RTLD_LAZY)))
      return false;

#define load(x) (glx.api.x = dlsym(glx.api.handle, (func = #x)))

   if (!load(glXChooseVisual))
      goto function_pointer_exception;
   if (!load(glXCreateContext))
      goto function_pointer_exception;
   if (!load(glXDestroyContext))
      goto function_pointer_exception;
   if (!load(glXMakeCurrent))
      goto function_pointer_exception;
   if (!load(glXSwapBuffers))
      goto function_pointer_exception;
   if (!load(glXQueryExtensionsString))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   wlc_glx_terminate();
   return false;
}

bool
wlc_glx_has_extension(const char *extension)
{
   assert(extension);

   if (!glx.extensions)
      return false;

   size_t len = strlen(extension), pos;
   const char *s = glx.extensions;
   while ((pos = strcspn(s, " ")) != 0) {
      size_t next = pos + (s[pos] != 0);

      if (!strncmp(s, extension, len))
         return true;

      s += next;
   }
   return false;
}

void
wlc_glx_swap_buffers(void)
{
   glx.api.glXSwapBuffers(wlc_x11_display(), wlc_x11_window());
}

void
wlc_glx_terminate(void)
{
   if (glx.has_current)
      glx.api.glXMakeCurrent(wlc_x11_display(), 0, NULL);

   if (glx.context)
      glx.api.glXDestroyContext(wlc_x11_display(), glx.context);

   if (glx.api.handle)
      dlclose(glx.api.handle);

   memset(&glx, 0, sizeof(glx));
   wlc_x11_terminate();
}

bool
wlc_glx_init(struct wl_display *display)
{
   (void)display;

   if (!wlc_x11_init())
      return false;

   if (!glx_load())
      return false;

   glx.extensions = glx.api.glXQueryExtensionsString(wlc_x11_display(), wlc_x11_screen());

   XVisualInfo *visual;
   GLint attributes[] = { GLX_RGBA, GLX_DOUBLEBUFFER, None };
   if (!(visual = glx.api.glXChooseVisual(wlc_x11_display(), 0, attributes))) {
      fprintf(stderr, "-!- glXChooseVisual failed\n");
      return false;
   }

   if (!(glx.context = glx.api.glXCreateContext(wlc_x11_display(), visual, NULL, GL_TRUE))) {
      fprintf(stderr, "-!- glXCreateContext failed\n");
      return false;
   }

   if (!glx.api.glXMakeCurrent(wlc_x11_display(), wlc_x11_window(), glx.context)) {
      fprintf(stderr, "-!- glXMakeCurrent failed\n");
      return false;
   }

   glx.has_current = true;
   fprintf(stdout, "-!- GLX context created\n");
   return true;
}

