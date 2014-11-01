#include "wlc_internal.h"
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

static void *bound = NULL;

struct ctx {
   const char *extensions;
   struct wlc_backend_surface *bsurface;
   struct wl_display *wl_display;
   EGLDisplay display;
   EGLContext context;
   EGLSurface surface;
   EGLConfig config;
   bool flip_failed;

   struct {
      // Needed for EGL hw surfaces
      PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
      PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
      PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
      PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
      PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
      PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC eglSwapBuffersWithDamage;
   } api;
};

static struct {
   struct {
      void *handle;
      EGLint (*eglGetError)(void);
      EGLDisplay (*eglGetDisplay)(NativeDisplayType);
      EGLBoolean (*eglInitialize)(EGLDisplay, EGLint*, EGLint*);
      EGLBoolean (*eglTerminate)(EGLDisplay);
      const char* (*eglQueryString)(EGLDisplay, EGLint name);
      EGLBoolean (*eglChooseConfig)(EGLDisplay, EGLint const*, EGLConfig*, EGLint, EGLint*);
      EGLBoolean (*eglBindAPI)(EGLenum);
      EGLBoolean (*eglQueryContext)(EGLDisplay, EGLContext, EGLint, EGLint*);
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
      PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC eglSwapBuffersWithDamage;
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
   if (!load(eglQueryContext))
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

static void
egl_call(const char *func, uint32_t line, const char *eglfunc)
{
   EGLint error;
   if ((error = egl.api.eglGetError()) == EGL_SUCCESS)
      return;

   wlc_log(WLC_LOG_ERROR, "egl: function %s at line %u: %s\n%s", func, line, eglfunc, egl_error_string(error));
}

#ifndef __STRING
#  define __STRING(x) #x
#endif

#define EGL_CALL(x) x; egl_call(__PRETTY_FUNCTION__, __LINE__, __STRING(x))

static bool
has_extension(const struct ctx *context, const char *extension)
{
   assert(context && extension);

   if (!context->extensions)
      return false;

   size_t len = strlen(extension), pos;
   const char *s = context->extensions;
   while ((pos = strcspn(s, " ")) != 0) {
      size_t next = pos + (s[pos] != 0);

      if (!strncmp(s, extension, len))
         return true;

      s += next;
   }

   return false;
}

static void
terminate(struct ctx *context)
{
   assert(context);

   EGL_CALL(egl.api.eglMakeCurrent(context->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));

   if (context->surface) {
      EGL_CALL(egl.api.eglDestroySurface(context->display, context->surface));
   }

   if (context->context) {
      EGL_CALL(egl.api.eglDestroyContext(context->display, context->context));
   }

   if (context->display) {
      if (context->api.eglUnbindWaylandDisplayWL && context->wl_display) {
         EGL_CALL(context->api.eglUnbindWaylandDisplayWL(context->display, context->wl_display));
      }

      // XXX: This is shared on all backends
      // egl.api.eglTerminate(context->display);
   }

   if (context->bsurface && context->bsurface->api.terminate)
      context->bsurface->api.terminate(context->bsurface);

   free(context);
}

static struct ctx*
create_context(struct wlc_backend_surface *surface)
{
   assert(surface);

   struct ctx *context;
   if (!(context = calloc(1, sizeof(struct ctx))))
      return NULL;

   if (!(context->display = egl.api.eglGetDisplay(surface->display)))
      goto egl_fail;

   EGLint major, minor;
   if (!egl.api.eglInitialize(context->display, &major, &minor))
      goto egl_fail;

   if (!egl.api.eglBindAPI(EGL_OPENGL_ES_API))
      goto egl_fail;

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

   EGLint n;
   if (!egl.api.eglChooseConfig(context->display, config_attribs, &context->config, 1, &n) || n < 1)
      goto egl_fail;

   static const EGLint context_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };

   if ((context->context = egl.api.eglCreateContext(context->display, context->config, EGL_NO_CONTEXT, context_attribs)) == EGL_NO_CONTEXT)
      goto egl_fail;

   if ((context->surface = egl.api.eglCreateWindowSurface(context->display, context->config, surface->window, NULL)) == EGL_NO_SURFACE)
      goto egl_fail;

   if (!egl.api.eglMakeCurrent(context->display, context->surface, context->surface, context->context))
      goto egl_fail;

   EGLint render_buffer;
   if (!egl.api.eglQueryContext(context->display, context->context, EGL_RENDER_BUFFER, &render_buffer))
      goto egl_fail;

   switch (render_buffer) {
      case EGL_SINGLE_BUFFER:
         wlc_log(WLC_LOG_INFO, "EGL context is single buffered");
         break;
      case EGL_BACK_BUFFER:
         wlc_log(WLC_LOG_INFO, "EGL context is double buffered");
         break;
      default:break;
   }

   const char *str;
   str = EGL_CALL(egl.api.eglQueryString(context->display, EGL_VERSION));
   wlc_log(WLC_LOG_INFO, "EGL version: %s", str ? str : "(null)");
   str = EGL_CALL(egl.api.eglQueryString(context->display, EGL_VENDOR));
   wlc_log(WLC_LOG_INFO, "EGL vendor: %s", str ? str : "(null)");
   str = EGL_CALL(egl.api.eglQueryString(context->display, EGL_CLIENT_APIS));
   wlc_log(WLC_LOG_INFO, "EGL client APIs: %s", str ? str : "(null)");

   context->extensions = EGL_CALL(egl.api.eglQueryString(context->display, EGL_EXTENSIONS));

   if (has_extension(context, "EGL_WL_bind_wayland_display") && has_extension(context, "EGL_KHR_image_base")) {
      context->api.eglCreateImageKHR = egl.api.eglCreateImageKHR;
      context->api.eglDestroyImageKHR = egl.api.eglDestroyImageKHR;
      context->api.eglBindWaylandDisplayWL = egl.api.eglBindWaylandDisplayWL;
      context->api.eglUnbindWaylandDisplayWL = egl.api.eglUnbindWaylandDisplayWL;
      context->api.eglQueryWaylandBufferWL = egl.api.eglQueryWaylandBufferWL;
   }

   if (has_extension(context, "EGL_EXT_swap_buffers_with_damage")) {
      // FIXME: get hw that supports this feature
      context->api.eglSwapBuffersWithDamage = egl.api.eglSwapBuffersWithDamage;
   } else {
      // FIXME: get hw that supports this feature
      // wlc_log(WLC_LOG_WARN, "EGL_EXT_swap_buffers_with_damage not supported. Performance could be affected.");
   }

   EGL_CALL(egl.api.eglSwapInterval(context->display, 1));
   return context;

egl_fail:
   {
      EGLint error;
      if ((error = egl.api.eglGetError()) != EGL_SUCCESS)
         wlc_log(WLC_LOG_WARN, "%s", egl_error_string(error));
   }
   terminate(context);
   return NULL;
}

static bool
bind(struct ctx *context)
{
   assert(context);

   EGLBoolean made_current = EGL_CALL(egl.api.eglMakeCurrent(context->display, context->surface, context->surface, context->context));
   if (made_current != EGL_TRUE)
      return false;

   bound = context;
   return true;
}

static bool
bind_to_wl_display(struct ctx *context, struct wl_display *wl_display)
{
   assert(context);

   if (wlc_no_egl_clients())
      return false;

   if (context->api.eglBindWaylandDisplayWL) {
      EGLBoolean binded = EGL_CALL(context->api.eglBindWaylandDisplayWL(context->display, wl_display));
      if (binded == EGL_TRUE)
         context->wl_display = wl_display;
   }

   return (context->wl_display ? true : false);
}

static void
swap(struct ctx *context)
{
   assert(context);

   EGLBoolean ret = EGL_FALSE;

   if (bound != context) {
      wlc_log(WLC_LOG_ERROR, "Bound context is wrong, eglSwapBuffers will fail!");
      abort();
   }

   if (!context->flip_failed)
      ret = EGL_CALL(egl.api.eglSwapBuffers(context->display, context->surface));

   if (ret == EGL_TRUE && context->bsurface->api.page_flip)
      context->flip_failed = !context->bsurface->api.page_flip(context->bsurface);
}

static EGLBoolean
query_buffer(struct ctx *context, struct wl_resource *buffer, EGLint attribute, EGLint *value)
{
   assert(context);
   if (context->api.eglQueryWaylandBufferWL)
      return EGL_CALL(context->api.eglQueryWaylandBufferWL(context->display, buffer, attribute, value));
   return EGL_FALSE;
}

static EGLImageKHR
create_image(struct ctx *context, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)
{
   assert(context);
   if (context->api.eglCreateImageKHR)
      return EGL_CALL(context->api.eglCreateImageKHR(context->display, context->context, target, buffer, attrib_list));
   return NULL;
}

static EGLBoolean
destroy_image(struct ctx *context, EGLImageKHR image)
{
   assert(context);
   if (context->api.eglDestroyImageKHR)
      return EGL_CALL(context->api.eglDestroyImageKHR(context->display, image));
   return EGL_FALSE;
}

static void
egl_unload(void)
{
   if (egl.api.handle)
      dlclose(egl.api.handle);

   memset(&egl, 0, sizeof(egl));
}

void*
wlc_egl_new(struct wlc_backend_surface *surface, struct wlc_context_api *api)
{
   assert(surface && api);

   if (!egl.api.handle && !egl_load()) {
      egl_unload();
      return NULL;
   }

   struct ctx *context;
   if (!(context = create_context(surface)))
      return NULL;

   context->bsurface = surface;

   api->terminate = terminate;
   api->bind = bind;
   api->bind_to_wl_display = bind_to_wl_display;
   api->swap = swap;
   api->destroy_image = destroy_image;
   api->create_image = create_image;
   api->query_buffer = query_buffer;
   wlc_log(WLC_LOG_INFO, "EGL context created");
   return context;
}

