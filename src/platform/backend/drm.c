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
#include "internal.h"
#include "macros.h"
#include "drm.h"
#include "backend.h"
#include "compositor/compositor.h"
#include "compositor/output.h"
#include "session/fd.h"

// FIXME: Contains global state (event_source && fd)

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
   bool flipping;
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

   struct wlc_output *o;
   wlc_output_finish_frame(wl_container_of(bsurface, o, bsurface), &ts);
   dsurface->flipping = false;
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
fail:
   release_fb(surface, fb);
   return false;
}

static bool
page_flip(struct wlc_backend_surface *bsurface)
{
   assert(bsurface && bsurface->internal);
   struct drm_surface *dsurface = bsurface->internal;
   assert(!dsurface->flipping);
   struct drm_fb *fb = &dsurface->fb[dsurface->index];
   release_fb(dsurface->surface, fb);

   struct wlc_output *o;
   except((o = wl_container_of(bsurface, o, bsurface)));

   if (!create_fb(dsurface->surface, fb))
      return false;

   if (fb->stride != dsurface->stride) {
      if (drm.api.drmModeSetCrtc(drm.fd, dsurface->crtc->crtc_id, fb->fd, 0, 0, &dsurface->connector->connector_id, 1, &dsurface->connector->modes[o->active.mode]))
         goto set_crtc_fail;

      dsurface->stride = fb->stride;
   }

   if (drm.api.drmModePageFlip(drm.fd, dsurface->crtc->crtc_id, fb->fd, DRM_MODE_PAGE_FLIP_EVENT, bsurface))
      goto failed_to_page_flip;

   dsurface->flipping = true;
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
surface_release(struct wlc_backend_surface *bsurface)
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
   struct wlc_backend_surface bsurface;
   if (!wlc_backend_surface(&bsurface, surface_release, sizeof(struct drm_surface)))
      return false;

   struct drm_surface *dsurface = bsurface.internal;
   dsurface->connector = info->connector;
   dsurface->encoder = info->encoder;
   dsurface->crtc = info->crtc;
   dsurface->surface = surface;
   dsurface->device = device;

   bsurface.display = (EGLNativeDisplayType)device;
   bsurface.window = (EGLNativeWindowType)surface;
   bsurface.api.sleep = surface_sleep;
   bsurface.api.page_flip = page_flip;

   struct wlc_output_event ev = { .add = { &bsurface, &info->info }, .type = WLC_OUTPUT_EVENT_ADD };
   wl_signal_emit(&wlc_system_signals()->output, &ev);
   return true;
}

static drmModeEncoder*
find_encoder_for_connector(int fd, drmModeRes *resources, drmModeConnector *connector, int32_t *out_crtc_id)
{
   assert(resources && connector && out_crtc_id);
   drmModeEncoder *encoder = drm.api.drmModeGetEncoder(fd, connector->encoder_id);

   if (encoder) {
      *out_crtc_id = encoder->crtc_id;
      return encoder;
   } else {
      drm.api.drmModeFreeEncoder(encoder);
      encoder = NULL;
   }

   for (int e = 0; e < resources->count_encoders; ++e) {
      if (!(encoder = drm.api.drmModeGetEncoder(fd, connector->encoder_id)))
         continue;

      for (int c = 0; c < resources->count_crtcs; ++c) {
         if (!(encoder->possible_crtcs & (1 << c)))
            continue;

         *out_crtc_id = resources->crtcs[c];
         return encoder;
      }

      drm.api.drmModeFreeEncoder(encoder);
   }

   return NULL;
}

static bool
query_drm(int fd, struct chck_iter_pool *out_infos)
{
   drmModeRes *resources;
   if (!(resources = drm.api.drmModeGetResources(fd))) {
      wlc_log(WLC_LOG_WARN, "Failed to get drm resources");
      goto resources_fail;
   }

   for (int c = 0; c < resources->count_connectors; c++) {
      drmModeConnector *connector;
      if (!(connector = drm.api.drmModeGetConnector(fd, resources->connectors[c]))) {
         wlc_log(WLC_LOG_WARN, "Failed to get connector %d", c);
         continue;
      }

      if (connector->connection != DRM_MODE_CONNECTED || connector->count_modes <= 0) {
         wlc_log(WLC_LOG_WARN, "Connector %d is not connected or has no modes", c);
         drm.api.drmModeFreeConnector(connector);
         continue;
      }

      int32_t crtc_id;
      drmModeEncoder *encoder;
      if (!(encoder = find_encoder_for_connector(fd, resources, connector, &crtc_id))) {
         wlc_log(WLC_LOG_WARN, "Failed to find encoder for connector %d", c);
         drm.api.drmModeFreeConnector(connector);
         continue;
      }

      drmModeCrtc *crtc;
      if (!(crtc = drm.api.drmModeGetCrtc(drm.fd, crtc_id))) {
         wlc_log(WLC_LOG_WARN, "Failed to get crtc for connector %d (with id: %d)", c, crtc_id);
         drm.api.drmModeFreeEncoder(encoder);
         drm.api.drmModeFreeConnector(connector);
         continue;
      }

      struct drm_output_information *info;
      if (!(info = chck_iter_pool_push_back(out_infos, NULL))) {
         drm.api.drmModeFreeCrtc(crtc);
         drm.api.drmModeFreeEncoder(encoder);
         drm.api.drmModeFreeConnector(connector);
         continue;
      }

      wlc_output_information(&info->info);
      chck_string_set_cstr(&info->info.make, "drm", false); // we can use colord for real info
      chck_string_set_cstr(&info->info.model, "unknown", false); // ^
      info->info.physical_width = connector->mmWidth;
      info->info.physical_height = connector->mmHeight;
      info->info.subpixel = connector->subpixel;
      info->info.scale = 1; // weston gets this from config?

      for (int i = 0; i < connector->count_modes; ++i) {
         struct wlc_output_mode mode = {0};
         mode.refresh = connector->modes[i].vrefresh * 1000; // mHz
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
output_exists_for_connector(struct chck_pool *outputs, drmModeConnector *connector)
{
   assert(outputs && connector);
   struct wlc_output *o;
   chck_pool_for_each(outputs, o) {
      struct drm_surface *dsurface = o->bsurface.internal;
      if (dsurface && dsurface->connector->connector_id == connector->connector_id)
         return true;
   }
   return false;
}

static bool
info_exists_for_drm_surface(struct chck_iter_pool *infos, struct drm_surface *dsurface)
{
   assert(infos && dsurface);
   struct drm_output_information *info;
   chck_iter_pool_for_each(infos, info) {
      if (dsurface->connector->connector_id == info->connector->connector_id)
         return true;
   }
   return false;
}

static uint32_t
update_outputs(struct chck_pool *outputs)
{
   struct chck_iter_pool infos;
   if (!chck_iter_pool(&infos, 4, 0, sizeof(struct drm_output_information)) || !query_drm(drm.fd, &infos))
      return 0;

   if (outputs) {
      struct wlc_output *o;
      chck_pool_for_each(outputs, o) {
         struct drm_surface *dsurface;
         if (!(dsurface = o->bsurface.internal))
            continue;

         if (!info_exists_for_drm_surface(&infos, dsurface))
            wlc_output_terminate(o);
      }
   }

   uint32_t count = 0;
   struct drm_output_information *info;
   chck_iter_pool_for_each(&infos, info) {
      if (outputs && output_exists_for_connector(outputs, info->connector))
         continue;

      struct gbm_surface *surface;
      if (!(surface = gbm.api.gbm_surface_create(gbm.device, info->width, info->height, GBM_BO_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)))
         continue;

      count += (add_output(gbm.device, surface, info) ? 1 : 0);
   }

   chck_iter_pool_release(&infos);
   return count;
}

bool
wlc_drm(struct wlc_backend *backend)
{
   drm.fd = -1;

   if (!gbm_load() || !drm_load())
      goto fail;

   const char *device = getenv("WLC_DRM_DEVICE");
   device = (chck_cstr_is_empty(device) ? "card0" : device);

   {
      struct chck_string path = {0};
      if (chck_string_set_format(&path, "/dev/dri/%s", device))
         drm.fd = wlc_fd_open(path.data, O_RDWR, WLC_FD_DRM);

      chck_string_release(&path);
   }

   if (drm.fd < 0)
      goto card_open_fail;

   /* GBM will load a dri driver, but even though they need symbols from
    * libglapi, in some version of Mesa they are not linked to it. Since
    * only the gl-renderer module links to it, the call above won't make
    * these symbols globally available, and loading the DRI driver fails.
    * Workaround this by dlopen()'ing libglapi with RTLD_GLOBAL. */
   dlopen("libglapi.so.0", RTLD_LAZY | RTLD_GLOBAL);

   if (!(gbm.device = gbm.api.gbm_create_device(drm.fd)))
      goto gbm_device_fail;

   if (!(drm.event_source = wl_event_loop_add_fd(wlc_event_loop(), drm.fd, WL_EVENT_READABLE, drm_event, NULL)))
      goto fail;

   backend->api.update_outputs = update_outputs;
   backend->api.terminate = terminate;
   return true;

card_open_fail:
   wlc_log(WLC_LOG_WARN, "Failed to open device: /dev/dri/%s", device);
   goto fail;
gbm_device_fail:
   wlc_log(WLC_LOG_WARN, "gbm_create_device failed");
fail:
   terminate();
   return false;
}
