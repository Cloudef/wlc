#include "egl.h"
#include "context.h"

#include "x11/x11.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <assert.h>

#include <EGL/egl.h>

static struct {
   EGLDisplay display;
   EGLContext context;
   EGLSurface surface;
   EGLConfig config;
   const char *extensions;
   bool has_current;

   struct {
      void *handle;
      EGLint (*eglGetError)(void);
      EGLDisplay (*eglGetDisplay)(NativeDisplayType);
      EGLBoolean (*eglInitialize)(EGLDisplay, EGLint*, EGLint*);
      EGLBoolean (*eglTerminate)(EGLDisplay);
      const char* (*eglQueryString)(EGLDisplay, EGLint name);
      EGLBoolean (*eglChooseConfig)(EGLDisplay, EGLint const*, EGLConfig*, EGLint, EGLint*);
      EGLBoolean (*eglBindAPI)(EGLenum);
      EGLContext (*eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, EGLint const*);
      EGLBoolean (*eglDestroyContext)(EGLDisplay, EGLContext);
      EGLSurface (*eglCreateWindowSurface)(EGLDisplay, EGLConfig, NativeWindowType, EGLint const*);
      EGLBoolean (*eglDestroySurface)(EGLDisplay, EGLSurface);
      EGLBoolean (*eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
      EGLBoolean (*eglSwapBuffers)(EGLDisplay, EGLSurface);
   } api;
} egl;

static bool
egl_load(void)
{
   const char *lib = "libEGL.so", *func = NULL;

   if (!(egl.api.handle = dlopen(lib, RTLD_LAZY)))
      return false;

#define load(x) (egl.api.x = dlsym(egl.api.handle, (func = #x)))

   if (!load(eglGetError))
      goto function_pointer_exception;
   if (!load(eglGetDisplay))
      goto function_pointer_exception;
   if (!load(eglInitialize))
      goto function_pointer_exception;
   if (!load(eglTerminate))
      goto function_pointer_exception;
   if (!load(eglQueryString))
      goto function_pointer_exception;
   if (!load(eglChooseConfig))
      goto function_pointer_exception;
   if (!load(eglBindAPI))
      goto function_pointer_exception;
   if (!load(eglCreateContext))
      goto function_pointer_exception;
   if (!load(eglDestroyContext))
      goto function_pointer_exception;
   if (!load(eglCreateWindowSurface))
      goto function_pointer_exception;
   if (!load(eglDestroySurface))
      goto function_pointer_exception;
   if (!load(eglMakeCurrent))
      goto function_pointer_exception;
   if (!load(eglSwapBuffers))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   return false;
}

static const char*
egl_error_string(const EGLint error)
{
    switch (error) {
        case EGL_SUCCESS:
            return "Success";
        case EGL_NOT_INITIALIZED:
            return "EGL is not or could not be initialized";
        case EGL_BAD_ACCESS:
            return "EGL cannot access a requested resource";
        case EGL_BAD_ALLOC:
            return "EGL failed to allocate resources for the requested operation";
        case EGL_BAD_ATTRIBUTE:
            return "An unrecognized attribute or attribute value was passed "
                   "in the attribute list";
        case EGL_BAD_CONTEXT:
            return "An EGLContext argument does not name a valid EGL "
                   "rendering context";
        case EGL_BAD_CONFIG:
            return "An EGLConfig argument does not name a valid EGL frame "
                   "buffer configuration";
        case EGL_BAD_CURRENT_SURFACE:
            return "The current surface of the calling thread is a window, pixel "
                   "buffer or pixmap that is no longer valid";
        case EGL_BAD_DISPLAY:
            return "An EGLDisplay argument does not name a valid EGL display "
                   "connection";
        case EGL_BAD_SURFACE:
            return "An EGLSurface argument does not name a valid surface "
                   "configured for GL rendering";
        case EGL_BAD_MATCH:
            return "Arguments are inconsistent";
        case EGL_BAD_PARAMETER:
            return "One or more argument values are invalid";
        case EGL_BAD_NATIVE_PIXMAP:
            return "A NativePixmapType argument does not refer to a valid "
                   "native pixmap";
        case EGL_BAD_NATIVE_WINDOW:
            return "A NativeWindowType argument does not refer to a valid "
                   "native window";
        case EGL_CONTEXT_LOST:
            return "The application must destroy all contexts and reinitialise";
    }

    return "UNKNOWN EGL ERROR";
}

static bool
has_extension(const char *extension)
{
   assert(extension);

   if (!egl.extensions)
      return false;

   size_t len = strlen(extension), pos;
   const char *s = egl.extensions;
   while ((pos = strcspn(s, " ")) != 0) {
      size_t next = pos + (s[pos] != 0);

      if (!strncmp(s, extension, len))
         return true;

      s += next;
   }
   return false;
}

static void
swap_buffers(void)
{
   egl.api.eglSwapBuffers(egl.display, egl.surface);
}

static void
terminate(void)
{
   if (egl.display) {
      if (egl.has_current)
         egl.api.eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

      if (egl.surface)
         egl.api.eglDestroySurface(egl.display, egl.surface);

      if (egl.context)
         egl.api.eglDestroyContext(egl.display, egl.context);

      egl.api.eglTerminate(egl.display);
   }

   if (egl.api.handle)
      dlclose(egl.api.handle);

   memset(&egl, 0, sizeof(egl));

   if (getenv("DISPLAY"))
      wlc_x11_terminate();
}

bool
wlc_egl_init(struct wl_display *display, struct wlc_context *out_context)
{
   (void)display;

   bool use_x11 = false;
   if (getenv("DISPLAY")) {
      if (!wlc_x11_init())
         return false;

      use_x11 = true;
   }

   if (!egl_load()) {
      terminate();
      return false;
   }

   static const EGLint context_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };

   static const EGLint config_attribs[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RED_SIZE, 1,
      EGL_GREEN_SIZE, 1,
      EGL_BLUE_SIZE, 1,
      EGL_DEPTH_SIZE, 1,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE
   };

   if (use_x11) {
      if (!(egl.display = egl.api.eglGetDisplay((EGLNativeDisplayType)wlc_x11_display())))
         goto fail;
   } else {
      if (!(egl.display = egl.api.eglGetDisplay((EGLNativeDisplayType)display)))
         goto fail;
   }

   EGLint major, minor;
   if (!egl.api.eglInitialize(egl.display, &major, &minor))
      goto fail;

   if (!egl.api.eglBindAPI(EGL_OPENGL_ES_API))
      goto fail;

   egl.extensions = egl.api.eglQueryString(egl.display, EGL_EXTENSIONS);

   EGLint n;
   if (!egl.api.eglChooseConfig(egl.display, config_attribs, &egl.config, 1, &n) || n < 1)
      goto fail;

   if ((egl.context = egl.api.eglCreateContext(egl.display, egl.config, EGL_NO_CONTEXT, context_attribs)) == EGL_NO_CONTEXT)
      goto fail;

   if ((egl.surface = egl.api.eglCreateWindowSurface(egl.display, egl.config, (NativeWindowType)(use_x11 ? wlc_x11_window() : 0), NULL)) == EGL_NO_SURFACE)
      goto fail;

   if (!egl.api.eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context))
      goto fail;

   egl.has_current = true;

   out_context->terminate = terminate;
   out_context->api.swap = swap_buffers;
   out_context->api.poll_events = wlc_x11_poll_events;
   out_context->api.event_fd = wlc_x11_event_fd;

   fprintf(stdout, "-!- EGL (%s) context created\n", (use_x11 ? "X11" : "FB"));
   return true;

fail:
   fprintf(stderr, "-!- %s\n", egl_error_string(egl.api.eglGetError()));
   terminate();
   return false;
}

