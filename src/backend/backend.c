#include "wlc.h"
#include "backend.h"
#include "x11.h"
#include "drm.h"

#include <stdlib.h>
#include <assert.h>

struct wlc_backend_surface*
wlc_backend_surface_new(void (*destructor)(struct wlc_backend_surface*), size_t internal_size)
{
   struct wlc_backend_surface *surface;
   if (!(surface = calloc(1, sizeof(struct wlc_backend_surface))))
      return NULL;

   if (internal_size > 0 && !(surface->internal = calloc(1, internal_size)))
      goto fail;

   surface->api.terminate = destructor;
   surface->internal_size = internal_size;
   return surface;

fail:
   free(surface);
   return NULL;
}

void wlc_backend_surface_free(struct wlc_backend_surface *surface)
{
   assert(surface);

   if (surface->api.terminate)
      surface->api.terminate(surface->internal);

   if (surface->internal_size > 0 && surface->internal)
      free(surface->internal);

   free(surface);
}

void
wlc_backend_terminate(struct wlc_backend *backend)
{
   assert(backend);
   backend->api.terminate();
   free(backend);
}

struct wlc_backend*
wlc_backend_init(struct wlc_compositor *compositor)
{
   struct wlc_backend *backend;

   if (!(backend = calloc(1, sizeof(struct wlc_backend))))
      goto out_of_memory;

   bool (*init[])(struct wlc_backend*, struct wlc_compositor*) = {
      wlc_x11_init,
      wlc_drm_init,
      NULL
   };

   for (int i = 0; init[i]; ++i)
      if (init[i](backend, compositor))
         return backend;

   wlc_log(WLC_LOG_WARN, "Could not initialize any backend");
   free(backend);
   return NULL;

out_of_memory:
   wlc_log(WLC_LOG_WARN, "Out of memory");
   return NULL;
}
