#include "wlc.h"
#include "egl.h"
#include "context.h"

#include "compositor/compositor.h"
#include "compositor/output.h"
#include "backend/backend.h"

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <assert.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <wayland-server.h>

struct egl_output {
   EGLContext context;
   EGLSurface surface;
   bool has_current;
   bool flip_failed;
};

static struct {
   struct wlc_backend *backend;
   struct wl_display *wl_display;

   EGLDisplay display;
   EGLConfig config;
   const char *extensions;

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
      EGLBoolean (*eglSwapInterval)(EGLDisplay, EGLint);

      // Needed for EGL hw surfaces
      PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
      PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
      PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
      PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
      PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
   } api;
} egl;

static bool
egl_load(void)
{
   const char *lib = "libEGL.so", *func = NULL;

   if (!(egl.api.handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

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
   if (!load(eglSwapInterval))
      goto function_pointer_exception;

   // EGL surfaces won't work without these
   load(eglCreateImageKHR);
   load(eglDestroyImageKHR);
   load(eglBindWaylandDisplayWL);
   load(eglUnbindWaylandDisplayWL);
   load(eglQueryWaylandBufferWL);

#undef load

   return true;

function_pointer_exception:
   wlc_log(WLC_LOG_WARN, "Could not load function '%s' from '%s'", func, lib);
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

#if 0
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
#endif

static void
swap_buffers(struct wlc_output *output)
{
   assert(output->context_info);
   struct egl_output *eglo = output->context_info;

   if (!eglo->flip_failed)
      egl.api.eglSwapBuffers(egl.display, eglo->surface);

   if (egl.backend->api.page_flip)
      eglo->flip_failed = !egl.backend->api.page_flip(output);
}

static void
terminate(void)
{
   if (egl.display) {
      if (egl.api.eglUnbindWaylandDisplayWL && egl.wl_display)
         egl.api.eglUnbindWaylandDisplayWL(egl.display, egl.wl_display);

      egl.api.eglTerminate(egl.display);
   }

   if (egl.api.handle)
      dlclose(egl.api.handle);

   memset(&egl, 0, sizeof(egl));
}

void
destroy(struct wlc_output *output)
{
   assert(output->context_info);
   struct egl_output *eglo = output->context_info;

   if (eglo->has_current)
      egl.api.eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

   if (eglo->surface)
      egl.api.eglDestroySurface(egl.display, eglo->surface);

   if (eglo->context)
      egl.api.eglDestroyContext(egl.display, eglo->context);

   free(output->context_info);
   output->context_info = NULL;
}

bool
attach(struct wlc_output *output)
{
   struct egl_output *eglo;
   if (!(output->context_info = eglo = calloc(1, sizeof(struct egl_output))))
      return false;

   static const EGLint context_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };

   if ((eglo->context = egl.api.eglCreateContext(egl.display, egl.config, EGL_NO_CONTEXT, context_attribs)) == EGL_NO_CONTEXT)
      goto fail;

   if ((eglo->surface = egl.api.eglCreateWindowSurface(egl.display, egl.config, egl.backend->api.window(output), NULL)) == EGL_NO_SURFACE)
      goto fail;

   if (!egl.api.eglMakeCurrent(egl.display, eglo->surface, eglo->surface, eglo->context))
      goto fail;

   return (eglo->has_current = true);

fail:
   destroy(output);
   return false;
}

static bool
bind(struct wlc_output *output)
{
   assert(output->context_info);
   struct egl_output *eglo = output->context_info;
   return (eglo->has_current ? egl.api.eglMakeCurrent(egl.display, eglo->surface, eglo->surface, eglo->context) : false);
}

EGLBoolean
wlc_egl_query_buffer(struct wl_resource *buffer, EGLint attribute, EGLint *value)
{
   if (egl.api.eglQueryWaylandBufferWL)
      return egl.api.eglQueryWaylandBufferWL(egl.display, buffer, attribute, value);
   return EGL_FALSE;
}

EGLImageKHR
wlc_egl_create_image(struct wlc_output *output, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)
{
   assert(output->context_info);
   struct egl_output *eglo = output->context_info;

   if (egl.api.eglCreateImageKHR)
      return egl.api.eglCreateImageKHR(egl.display, eglo->context, target, buffer, attrib_list);
   return NULL;
}

EGLBoolean
wlc_egl_destroy_image(EGLImageKHR image)
{
   if (egl.api.eglDestroyImageKHR)
      return egl.api.eglDestroyImageKHR(egl.display, image);
   return EGL_FALSE;
}

bool
wlc_egl_init(struct wlc_compositor *compositor, struct wlc_backend *backend, struct wlc_context *out_context)
{
   if (!egl_load())
      goto fail;

   static const EGLint config_attribs[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RED_SIZE, 1,
      EGL_GREEN_SIZE, 1,
      EGL_BLUE_SIZE, 1,
      EGL_ALPHA_SIZE, 0,
      EGL_DEPTH_SIZE, 1,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE
   };

   egl.backend = backend;

   // FIXME: output ignored for now, but it's possible for outputs to have different display
   if (!(egl.display = egl.api.eglGetDisplay(egl.backend->api.display(NULL))))
      goto egl_fail;

   EGLint major, minor;
   if (!egl.api.eglInitialize(egl.display, &major, &minor))
      goto egl_fail;

   if (!egl.api.eglBindAPI(EGL_OPENGL_ES_API))
      goto egl_fail;

   const char *str;
   str = egl.api.eglQueryString(egl.display, EGL_VERSION);
   wlc_log(WLC_LOG_INFO, "EGL version: %s", str ? str : "(null)");
   str = egl.api.eglQueryString(egl.display, EGL_VENDOR);
   wlc_log(WLC_LOG_INFO, "EGL vendor: %s", str ? str : "(null)");
   str = egl.api.eglQueryString(egl.display, EGL_CLIENT_APIS);
   wlc_log(WLC_LOG_INFO, "EGL client APIs: %s", str ? str : "(null)");

   egl.extensions = egl.api.eglQueryString(egl.display, EGL_EXTENSIONS);

   EGLint n;
   if (!egl.api.eglChooseConfig(egl.display, config_attribs, &egl.config, 1, &n) || n < 1)
      goto egl_fail;

   if (!strstr(egl.extensions, "EGL_WL_bind_wayland_display")) {
      egl.api.eglBindWaylandDisplayWL = NULL;
      egl.api.eglUnbindWaylandDisplayWL = NULL;
   }

   if (egl.api.eglBindWaylandDisplayWL && egl.api.eglBindWaylandDisplayWL(egl.display, compositor->display))
      egl.wl_display = compositor->display;

   egl.api.eglSwapInterval(egl.display, 1);

   wl_display_add_shm_format(compositor->display, WL_SHM_FORMAT_RGB565);

   out_context->terminate = terminate;
   out_context->api.bind = bind;
   out_context->api.attach = attach;
   out_context->api.destroy = destroy;
   out_context->api.swap = swap_buffers;
   wlc_log(WLC_LOG_INFO, "EGL (%s) context created", egl.backend->name);
   return true;

egl_fail:
   {
      EGLint error;
      if ((error = egl.api.eglGetError()) != EGL_SUCCESS)
         wlc_log(WLC_LOG_WARN, "%s", egl_error_string(error));
   }
fail:
   terminate();
   return false;
}

