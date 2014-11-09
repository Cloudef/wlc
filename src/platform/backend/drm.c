#include "internal.h"
#include "drm.h"
#include "backend.h"

#include "compositor/compositor.h"
#include "compositor/output.h"

#include "session/fd.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <dlfcn.h>

#include <wayland-server.h>
#include <wayland-util.h>

// FIXME: contains global state (event_source && fd)

#define NUM_FBS 2

struct drm_output_information {
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   drmModeCrtc *crtc;
   struct wlc_output_information info;
   uint32_t width, height;
};

struct drm_surface {
   struct gbm_device *device;
   struct gbm_surface *surface;
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   drmModeCrtc *crtc;

   struct drm_fb {
      struct gbm_bo *bo;
      uint32_t fd;
      uint32_t stride;
   } fb[NUM_FBS];

   uint32_t stride;
   uint8_t index;
};

static struct {
   struct gbm_device *device;

   struct {
      void *handle;

      struct gbm_device* (*gbm_create_device)(int);
      void (*gbm_device_destroy)(struct gbm_device*);
      struct gbm_surface* (*gbm_surface_create)(struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t);
      void (*gbm_surface_destroy)(struct gbm_surface*);
      uint32_t (*gbm_bo_get_width)(struct gbm_bo*);
      uint32_t (*gbm_bo_get_height)(struct gbm_bo*);
      uint32_t (*gbm_bo_get_stride)(struct gbm_bo*);
      union gbm_bo_handle (*gbm_bo_get_handle)(struct gbm_bo*);
      int (*gbm_surface_has_free_buffers)(struct gbm_surface*);
      struct gbm_bo* (*gbm_surface_lock_front_buffer)(struct gbm_surface*);
      int (*gbm_surface_release_buffer)(struct gbm_surface*, struct gbm_bo*);
   } api;
} gbm;

static struct {
   int fd;
   struct wl_event_source *event_source;

   struct {
      void *handle;

      int (*drmIoctl)(int fd, unsigned long request, void *arg);
      int (*drmModeAddFB)(int, uint32_t, uint32_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t*);
      int (*drmModeRmFB)(int, uint32_t);
      int (*drmModePageFlip)(int, uint32_t, uint32_t, uint32_t, void*);
      int (*drmModeSetCrtc)(int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, int, drmModeModeInfoPtr);
      int (*drmHandleEvent)(int, drmEventContextPtr);
      drmModeResPtr (*drmModeGetResources)(int);
      drmModeCrtcPtr (*drmModeGetCrtc)(int, uint32_t);
      void (*drmModeFreeCrtc)(drmModeCrtcPtr);
      drmModeConnectorPtr (*drmModeGetConnector)(int, uint32_t);
      void (*drmModeFreeConnector)(drmModeConnectorPtr);
      drmModeEncoderPtr (*drmModeGetEncoder)(int, uint32_t);
      void (*drmModeFreeEncoder)(drmModeEncoderPtr);
   } api;
} drm;

static bool
gbm_load(void)
{
   const char *lib = "libgbm.so", *func = NULL;

   if (!(gbm.api.handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (gbm.api.x = dlsym(gbm.api.handle, (func = #x)))

   if (!load(gbm_create_device))
      goto function_pointer_exception;
   if (!load(gbm_device_destroy))
      goto function_pointer_exception;
   if (!load(gbm_surface_create))
      goto function_pointer_exception;
   if (!load(gbm_surface_destroy))
      goto function_pointer_exception;
   if (!load(gbm_bo_get_handle))
      goto function_pointer_exception;
   if (!load(gbm_bo_get_width))
      goto function_pointer_exception;
   if (!load(gbm_bo_get_height))
      goto function_pointer_exception;
   if (!load(gbm_bo_get_stride))
      goto function_pointer_exception;
   if (!load(gbm_surface_has_free_buffers))
      goto function_pointer_exception;
   if (!load(gbm_surface_lock_front_buffer))
      goto function_pointer_exception;
   if (!load(gbm_surface_release_buffer))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   wlc_log(WLC_LOG_WARN, "Could not load function '%s' from '%s'", func, lib);
   return false;
}

static bool
drm_load(void)
{
   const char *lib = "libdrm.so", *func = NULL;

   if (!(drm.api.handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (drm.api.x = dlsym(drm.api.handle, (func = #x)))

   if (!load(drmIoctl))
      goto function_pointer_exception;
   if (!load(drmModeAddFB))
      goto function_pointer_exception;
   if (!load(drmModeRmFB))
      goto function_pointer_exception;
   if (!load(drmModePageFlip))
      goto function_pointer_exception;
   if (!load(drmModeSetCrtc))
      goto function_pointer_exception;
   if (!load(drmHandleEvent))
      goto function_pointer_exception;
   if (!load(drmModeGetResources))
      goto function_pointer_exception;
   if (!load(drmModeGetCrtc))
      goto function_pointer_exception;
   if (!load(drmModeFreeCrtc))
      goto function_pointer_exception;
   if (!load(drmModeGetConnector))
      goto function_pointer_exception;
   if (!load(drmModeFreeConnector))
      goto function_pointer_exception;
   if (!load(drmModeGetEncoder))
      goto function_pointer_exception;
   if (!load(drmModeFreeEncoder))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   wlc_log(WLC_LOG_WARN, "Could not load function '%s' from '%s'", func, lib);
   return false;
}

static void
release_fb(struct gbm_surface *surface, struct drm_fb *fb)
{
   assert(surface && fb);

   if (fb->fd > 0)
      drm.api.drmModeRmFB(drm.fd, fb->fd);

   if (fb->bo)
      gbm.api.gbm_surface_release_buffer(surface, fb->bo);

   fb->bo = NULL;
   fb->fd = 0;
}

static void
page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data)
{
   assert(data);
   (void)fd, (void)frame;
   struct wlc_backend_surface *bsurface = data;
   struct drm_surface *dsurface = bsurface->internal;

   uint8_t next = (dsurface->index + 1) % NUM_FBS;
   release_fb(dsurface->surface, &dsurface->fb[next]);
   dsurface->index = next;

   struct timespec ts;
   ts.tv_sec = sec;
   ts.tv_nsec = usec * 1000;
   wlc_output_finish_frame(bsurface->output, &ts);
}

static int
drm_event(int fd, uint32_t mask, void *data)
{
   (void)mask, (void)data;
   drmEventContext evctx;
   memset(&evctx, 0, sizeof(evctx));
   evctx.version = DRM_EVENT_CONTEXT_VERSION;
   evctx.page_flip_handler = page_flip_handler;
   drm.api.drmHandleEvent(fd, &evctx);
   return 0;
}

static bool
create_fb(struct gbm_surface *surface, struct drm_fb *fb)
{
   assert(surface && fb);

   if (!gbm.api.gbm_surface_has_free_buffers(surface))
      goto no_buffers;

   if (!(fb->bo = gbm.api.gbm_surface_lock_front_buffer(surface)))
      goto failed_to_lock;

   uint32_t width = gbm.api.gbm_bo_get_width(fb->bo);
   uint32_t height = gbm.api.gbm_bo_get_height(fb->bo);
   uint32_t handle = gbm.api.gbm_bo_get_handle(fb->bo).u32;
   uint32_t stride = gbm.api.gbm_bo_get_stride(fb->bo);

   if (drm.api.drmModeAddFB(drm.fd, width, height, 24, 32, stride, handle, &fb->fd))
      goto failed_to_create_fb;

   fb->stride = stride;
   return true;

no_buffers:
   wlc_log(WLC_LOG_WARN, "gbm is out of buffers");
   goto fail;
failed_to_lock:
   wlc_log(WLC_LOG_WARN, "Failed to lock front buffer");
   goto fail;
failed_to_create_fb:
   wlc_log(WLC_LOG_WARN, "Failed to create fb");
   goto fail;
fail:
   release_fb(surface, fb);
   return false;
}

static bool
page_flip(struct wlc_backend_surface *bsurface)
{
   assert(bsurface && bsurface->internal);
   struct drm_surface *dsurface = bsurface->internal;
   struct drm_fb *fb = &dsurface->fb[dsurface->index];
   release_fb(dsurface->surface, fb);

   if (!create_fb(dsurface->surface, fb))
      return false;

   if (fb->stride != dsurface->stride) {
      if (drm.api.drmModeSetCrtc(drm.fd, dsurface->encoder->crtc_id, fb->fd, 0, 0, &dsurface->connector->connector_id, 1, &dsurface->connector->modes[bsurface->output->mode]))
         goto set_crtc_fail;

      dsurface->stride = fb->stride;
   }

   if (drm.api.drmModePageFlip(drm.fd, dsurface->encoder->crtc_id, fb->fd, DRM_MODE_PAGE_FLIP_EVENT, bsurface))
      goto failed_to_page_flip;

   return true;

set_crtc_fail:
   wlc_log(WLC_LOG_WARN, "Failed to set mode: %m");
   goto fail;
failed_to_page_flip:
   wlc_log(WLC_LOG_WARN, "Failed to page flip: %m");
fail:
   release_fb(dsurface->surface, fb);
   return false;
}

static void
surface_sleep(struct wlc_backend_surface *bsurface, bool sleep)
{
   struct drm_surface *dsurface = bsurface->internal;

   if (sleep) {
      drm.api.drmModeSetCrtc(drm.fd, dsurface->crtc->crtc_id, 0, 0, 0, NULL, 0, NULL);
      dsurface->stride = 0;
   }
}

static void
surface_free(struct wlc_backend_surface *bsurface)
{
   struct drm_surface *dsurface = bsurface->internal;
   struct drm_fb *fb = &dsurface->fb[dsurface->index];
   release_fb(dsurface->surface, fb);

   drm.api.drmModeSetCrtc(drm.fd, dsurface->crtc->crtc_id, dsurface->crtc->buffer_id, dsurface->crtc->x, dsurface->crtc->y, &dsurface->connector->connector_id, 1, &dsurface->crtc->mode);

   if (dsurface->crtc)
      drm.api.drmModeFreeCrtc(dsurface->crtc);

   if (dsurface->surface)
      gbm.api.gbm_surface_destroy(dsurface->surface);

   if (dsurface->encoder)
      drm.api.drmModeFreeEncoder(dsurface->encoder);

   if (dsurface->connector)
      drm.api.drmModeFreeConnector(dsurface->connector);

   wlc_log(WLC_LOG_INFO, "Released drm surface (%p)", bsurface);
}

static bool
add_output(struct gbm_device *device, struct gbm_surface *surface, struct drm_output_information *info)
{
   struct wlc_backend_surface *bsurface = NULL;
   if (!(bsurface = wlc_backend_surface_new(surface_free, sizeof(struct drm_surface))))
      return false;

   struct drm_surface *dsurface = bsurface->internal;
   dsurface->connector = info->connector;
   dsurface->encoder = info->encoder;
   dsurface->crtc = info->crtc;
   dsurface->surface = surface;
   dsurface->device = device;

   bsurface->display = (EGLNativeDisplayType)device;
   bsurface->window = (EGLNativeWindowType)surface;
   bsurface->api.sleep = surface_sleep;
   bsurface->api.page_flip = page_flip;

   struct wlc_output_event ev = { .add = { bsurface, &info->info }, .type = WLC_OUTPUT_EVENT_ADD };
   wl_signal_emit(&wlc_system_signals()->output, &ev);
   return true;
}

static drmModeEncoder*
find_encoder_for_connector(const int fd, drmModeRes *resources, drmModeConnector *connector)
{
   for (int i = 0; i < resources->count_encoders; i++) {
      drmModeEncoder *encoder;
      if (!(encoder = drm.api.drmModeGetEncoder(fd, resources->encoders[i])))
         continue;

      if (encoder->encoder_id == connector->encoder_id)
         return encoder;

      drm.api.drmModeFreeEncoder(encoder);
   }

   return NULL;
}

static bool
query_drm(int fd, struct wl_array *out_infos)
{
   drmModeRes *resources;
   if (!(resources = drm.api.drmModeGetResources(fd)))
      goto resources_fail;

   for (int c = 0; c < resources->count_connectors; c++) {
      drmModeConnector *connector;
      if (!(connector = drm.api.drmModeGetConnector(fd, resources->connectors[c])))
         continue;

      if (connector->connection != DRM_MODE_CONNECTED || connector->count_modes <= 0) {
         drm.api.drmModeFreeConnector(connector);
         continue;
      }

      drmModeEncoder *encoder;
      if (!(encoder = find_encoder_for_connector(fd, resources, connector))) {
         drm.api.drmModeFreeConnector(connector);
         continue;
      }

      drmModeCrtc *crtc;
      if (!(crtc = drm.api.drmModeGetCrtc(drm.fd, encoder->crtc_id))) {
         drm.api.drmModeFreeEncoder(encoder);
         drm.api.drmModeFreeConnector(connector);
         continue;
      }

      struct drm_output_information *info;
      if (!(info = wl_array_add(out_infos, sizeof(struct drm_output_information)))) {
         drm.api.drmModeFreeCrtc(crtc);
         drm.api.drmModeFreeEncoder(encoder);
         drm.api.drmModeFreeConnector(connector);
         continue;
      }

      memset(info, 0, sizeof(struct drm_output_information));
      wlc_string_set(&info->info.make, "drm", false); // we can use colord for real info
      wlc_string_set(&info->info.model, "unknown", false); // ^
      info->info.physical_width = connector->mmWidth;
      info->info.physical_height = connector->mmHeight;
      info->info.subpixel = connector->subpixel;

      for (int i = 0; i < connector->count_modes; ++i) {
         struct wlc_output_mode mode;
         memset(&mode, 0, sizeof(mode));
         mode.refresh = connector->modes[i].vrefresh;
         mode.width = connector->modes[i].hdisplay;
         mode.height = connector->modes[i].vdisplay;

         if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            mode.flags |= WL_OUTPUT_MODE_PREFERRED;
            if (!info->width && !info->height) {
               info->width = connector->modes[i].hdisplay;
               info->height = connector->modes[i].vdisplay;
            }
         }

         if (crtc->mode_valid && !memcmp(&connector->modes[i], &crtc->mode, sizeof(crtc->mode))) {
            mode.flags |= WL_OUTPUT_MODE_CURRENT;
            info->width = connector->modes[i].hdisplay;
            info->height = connector->modes[i].vdisplay;
         }

         wlc_log(WLC_LOG_INFO, "MODE: (%d) %ux%u %s", c, mode.width, mode.height, (mode.flags & WL_OUTPUT_MODE_CURRENT ? "*" : (mode.flags & WL_OUTPUT_MODE_PREFERRED ? "!" : "")));
         wlc_output_information_add_mode(&info->info, &mode);
      }

      info->crtc = crtc;
      info->encoder = encoder;
      info->connector = connector;
   }

   return true;

resources_fail:
   wlc_log(WLC_LOG_WARN, "drmModeGetResources failed");
   goto fail;
fail:
   memset(&drm, 0, sizeof(drm));
   return false;
}

static void
terminate(void)
{
   if (drm.event_source)
      wl_event_source_remove(drm.event_source);

   if (gbm.device)
      gbm.api.gbm_device_destroy(gbm.device);

   if (drm.api.handle)
      dlclose(drm.api.handle);

   if (gbm.api.handle)
      dlclose(gbm.api.handle);

   wlc_fd_close(drm.fd);

   memset(&drm, 0, sizeof(drm));
   memset(&gbm, 0, sizeof(gbm));

   wlc_log(WLC_LOG_INFO, "Closed drm");
}

static bool
output_exists_for_connector(struct wl_list *outputs, drmModeConnector *connector)
{
   assert(outputs && connector);
   struct wlc_output *o;
   wl_list_for_each(o, outputs, link) {
      struct drm_surface *dsurface = (o->bsurface ? o->bsurface->internal : NULL);
      if (dsurface && dsurface->connector->connector_id == connector->connector_id)
         return true;
   }
   return false;
}

static bool
info_exists_for_drm_surface(struct wl_array *infos, struct drm_surface *dsurface)
{
   assert(infos && dsurface);
   struct drm_output_information *info;
   wl_array_for_each(info, infos) {
      if (dsurface->connector->connector_id == info->connector->connector_id)
         return true;
   }
   return false;
}

static uint32_t
update_outputs(struct wl_list *outputs)
{
   struct wl_array infos;
   wl_array_init(&infos);
   if (!query_drm(drm.fd, &infos))
      return 0;

   if (outputs) {
      struct wlc_output *o, *on;
      wl_list_for_each_safe(o, on, outputs, link) {
         struct drm_surface *dsurface = (o->bsurface ? o->bsurface->internal : NULL);
         if (!dsurface)
            continue;

         if (!info_exists_for_drm_surface(&infos, dsurface))
            wlc_output_terminate(o);
      }
   }

   uint32_t count = 0;
   struct drm_output_information *info;
   wl_array_for_each(info, &infos) {
      if (outputs && output_exists_for_connector(outputs, info->connector))
         continue;

      struct gbm_surface *surface;
      if (!(surface = gbm.api.gbm_surface_create(gbm.device, info->width, info->height, GBM_BO_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)))
         continue;

      count += (add_output(gbm.device, surface, info) ? 1 : 0);
   }

   wl_array_release(&infos);
   return count;
}

bool
wlc_drm_init(struct wlc_backend *out_backend, struct wlc_compositor *compositor)
{
   (void)compositor;

   if (!gbm_load() || !drm_load())
      goto fail;

   if ((drm.fd = wlc_fd_open("/dev/dri/card0", O_RDWR, WLC_FD_DRM)) < 0)
      goto card_open_fail;

   /* GBM will load a dri driver, but even though they need symbols from
    * libglapi, in some version of Mesa they are not linked to it. Since
    * only the gl-renderer module links to it, the call above won't make
    * these symbols globally available, and loading the DRI driver fails.
    * Workaround this by dlopen()'ing libglapi with RTLD_GLOBAL. */
   dlopen("libglapi.so.0", RTLD_LAZY | RTLD_GLOBAL);

   if (!(gbm.device = gbm.api.gbm_create_device(drm.fd)))
      goto gbm_device_fail;

   if (!update_outputs(NULL))
      goto fail;

   if (!(drm.event_source = wl_event_loop_add_fd(wlc_event_loop(), drm.fd, WL_EVENT_READABLE, drm_event, NULL)))
      goto fail;

   out_backend->api.update_outputs = update_outputs;
   out_backend->api.terminate = terminate;
   return true;

card_open_fail:
   wlc_log(WLC_LOG_WARN, "Failed to open card: /dev/dri/card0");
   goto fail;
gbm_device_fail:
   wlc_log(WLC_LOG_WARN, "gbm_create_device failed");
fail:
   terminate();
   return false;
}
