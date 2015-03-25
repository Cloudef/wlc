#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wlc/wlc.h>
#include "context.h"
#include "egl.h"

EGLBoolean
wlc_context_query_buffer(struct wlc_context *context, struct wl_resource *buffer, EGLint attribute, EGLint *value)
{
   assert(context && buffer);

   if (!context->api.query_buffer)
      return EGL_FALSE;

   return context->api.query_buffer(context->context, buffer, attribute, value);
}

EGLImageKHR
wlc_context_create_image(struct wlc_context *context, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)
{
   assert(context && buffer);

   if (!context->api.create_image)
      return 0;

   return context->api.create_image(context->context, target, buffer, attrib_list);
}

EGLBoolean
wlc_context_destroy_image(struct wlc_context *context, EGLImageKHR image)
{
   assert(context && image);

   if (!context->api.destroy_image)
      return EGL_FALSE;

   return context->api.destroy_image(context->context, image);
}

bool
wlc_context_bind(struct wlc_context *context)
{
   assert(context);

   if (!context->api.bind || !context->api.bind(context->context))
      goto fail;

   return true;

fail:
   wlc_log(WLC_LOG_ERROR, "Failed to bind context");
   return false;
}

bool
wlc_context_bind_to_wl_display(struct wlc_context *context, struct wl_display *display)
{
   assert(context);

   if (!context->api.bind_to_wl_display)
      return false;

   return context->api.bind_to_wl_display(context->context, display);
}

void
wlc_context_swap(struct wlc_context *context, struct wlc_backend_surface *bsurface)
{
   assert(context);

   if (context->api.swap)
      context->api.swap(context->context, bsurface);
}

void
wlc_context_release(struct wlc_context *context)
{
   if (!context)
      return;

   if (context->api.terminate)
      context->api.terminate(context->context);

   memset(context, 0, sizeof(struct wlc_context));
}

bool
wlc_context(struct wlc_context *context, struct wlc_backend_surface *surface)
{
   assert(surface);
   memset(context, 0, sizeof(struct wlc_context));

   void* (*constructor[])(struct wlc_backend_surface*, struct wlc_context_api*) = {
      wlc_egl,
      NULL
   };

   for (uint32_t i = 0; constructor[i]; ++i) {
      if ((context->context = constructor[i](surface, &context->api)))
         return true;
   }

   wlc_log(WLC_LOG_WARN, "Could not initialize any context");
   return false;
}
