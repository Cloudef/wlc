#include "wlc.h"
#include "context.h"
#include "egl.h"

#include <stdlib.h>
#include <assert.h>

struct wlc_context {
   void *context; // internal surface context (EGL, etc)
   struct wlc_context_api api;
};

EGLBoolean
wlc_context_query_buffer(struct wlc_context *context, struct wl_resource *buffer, EGLint attribute, EGLint *value)
{
   assert(context && buffer);
   return context->api.query_buffer(context->context, buffer, attribute, value);
}

EGLImageKHR
wlc_context_create_image(struct wlc_context *context, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)
{
   assert(context && buffer);
   return context->api.create_image(context->context, target, buffer, attrib_list);
}

EGLBoolean
wlc_context_destroy_image(struct wlc_context *context, EGLImageKHR image)
{
   assert(context && image);
   return context->api.destroy_image(context->context, image);
}

bool
wlc_context_bind(struct wlc_context *context)
{
   assert(context);
   return context->api.bind(context->context);
}

bool
wlc_context_bind_to_wl_display(struct wlc_context *context, struct wl_display *display)
{
   assert(context);
   return context->api.bind_to_wl_display(context->context, display);
}

void
wlc_context_swap(struct wlc_context *context)
{
   assert(context);
   context->api.swap(context->context);
}

void
wlc_context_free(struct wlc_context *context)
{
   assert(context);
   context->api.terminate(context->context);
   free(context);
}

struct wlc_context*
wlc_context_new(struct wlc_backend_surface *surface)
{
   assert(surface);

   struct wlc_context *context;
   if (!(context = calloc(1, sizeof(struct wlc_context))))
      goto out_of_memory;

   void* (*constructor[])(struct wlc_backend_surface*, struct wlc_context_api*) = {
      wlc_egl_new,
      NULL
   };

   for (int i = 0; constructor[i]; ++i) {
      if ((context->context = constructor[i](surface, &context->api)))
         return context;
   }

   wlc_log(WLC_LOG_WARN, "Could not initialize any context");
   free(context);
   return NULL;

out_of_memory:
   wlc_log(WLC_LOG_WARN, "Out of memory");
   return NULL;
}
