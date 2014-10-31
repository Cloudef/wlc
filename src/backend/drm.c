#include "wlc_internal.h"
#include "drm.h"
#include "backend.h"

#include "udev/udev.h"

#include "compositor/compositor.h"
#include "compositor/output.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <dlfcn.h>

#include <wayland-server.h>
#include <wayland-util.h>

// FIXME: contains global state

#define NUM_FBS 2

struct drm_output_information {
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   struct wlc_output_information info;
   uint32_t width, height;
};

struct drm_surface {
   struct wlc_output *output;
   struct gbm_device *device;
   struct gbm_surface *surface;
   drmModeConnector *connector;
   drmModeEncoder *encoder;

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

      int (*drmSetMaster)(int);
      int (*drmDropMaster)(int);
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

static struct {
   struct wlc_udev *udev;
} seat;

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

   if (!load(drmSetMaster))
      goto function_pointer_exception;
   if (!load(drmDropMaster))
      goto function_pointer_exception;
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
   assert(fb);

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
   (void)fd, (void)frame, (void)sec, (void)usec;
   struct drm_surface *dsurface = data;

   uint8_t next = (dsurface->index + 1) % NUM_FBS;
   release_fb(dsurface->surface, &dsurface->fb[next]);
   dsurface->index = next;
   dsurface->output->pending = false;

   struct timespec ts;
   ts.tv_sec = sec;
   ts.tv_nsec = usec * 1000;
   wlc_output_finish_frame(dsurface->output, &ts);
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
   struct drm_surface *dsurface = bsurface->internal;
   assert(!dsurface->output->pending);
   struct drm_fb *fb = &dsurface->fb[dsurface->index];

   if (fb->bo)
      release_fb(dsurface->surface, fb);

   if (!create_fb(dsurface->surface, fb))
      return false;

   if (fb->stride != dsurface->stride) {
      if (drm.api.drmModeSetCrtc(drm.fd, dsurface->encoder->crtc_id, fb->fd, 0, 0, &dsurface->connector->connector_id, 1, &dsurface->connector->modes[dsurface->output->mode]))
         goto set_crtc_fail;

      dsurface->stride = fb->stride;
   }

   if (drm.api.drmModePageFlip(drm.fd, dsurface->encoder->crtc_id, fb->fd, DRM_MODE_PAGE_FLIP_EVENT, dsurface))
      goto failed_to_page_flip;

   dsurface->output->pending = true;
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
surface_free(struct wlc_backend_surface *bsurface)
{
   struct drm_surface *surface = bsurface->internal;

   if (surface->surface)
      gbm.api.gbm_surface_destroy(surface->surface);

   if (surface->encoder)
      drm.api.drmModeFreeEncoder(surface->encoder);

   if (surface->connector)
      drm.api.drmModeFreeConnector(surface->connector);

   wlc_backend_surface_free(bsurface);
}

static bool
add_output(struct wlc_compositor *compositor, struct gbm_device *device, struct gbm_surface *surface, struct drm_output_information *info)
{
   struct wlc_backend_surface *bsurface = NULL;
   if (!(bsurface = wlc_backend_surface_new(surface_free, sizeof(struct drm_surface))))
      goto fail;

   struct drm_surface *dsurface = bsurface->internal;
   dsurface->connector = info->connector;
   dsurface->encoder = info->encoder;
   dsurface->surface = surface;
   dsurface->device = device;

   bsurface->display = (EGLNativeDisplayType)device;
   bsurface->window = (EGLNativeWindowType)surface;
   bsurface->api.page_flip = page_flip;

   if (!(dsurface->output = wlc_output_new(compositor, bsurface, &info->info)))
      goto fail;

   if (!compositor->api.add_output(compositor, dsurface->output))
      goto fail;

   return true;

fail:
   if (bsurface)
      wlc_backend_surface_free(bsurface);
   if (bsurface && dsurface->output)
      wlc_output_free(dsurface->output);
   return false;
}

static int
remove_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   compositor->api.remove_output(compositor, output);
   wlc_output_free(output);
   return wl_list_length(&compositor->outputs);
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
setup_drm(int fd, struct wl_array *out_infos)
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

      drm.api.drmModeFreeCrtc(crtc);
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

static bool
set_master(void)
{
   return !drm.api.drmSetMaster(drm.fd);
}

static bool
drop_master(void)
{
   return !drm.api.drmDropMaster(drm.fd);
}

static void
terminate(void)
{
   if (gbm.device)
      gbm.api.gbm_device_destroy(gbm.device);

   if (drm.event_source)
      wl_event_source_remove(drm.event_source);

   if (drm.api.handle)
      dlclose(drm.api.handle);

   if (gbm.api.handle)
      dlclose(gbm.api.handle);

   wlc_set_drm_control_functions(NULL, NULL);

   memset(&drm, 0, sizeof(drm));
   memset(&gbm, 0, sizeof(gbm));
}

bool
wlc_drm_init(struct wlc_backend *out_backend, struct wlc_compositor *compositor)
{
   if (!gbm_load() || !drm_load())
      goto fail;

   if ((drm.fd = open("/dev/dri/card0", O_RDWR)) < 0)
      goto card_open_fail;

   /* GBM will load a dri driver, but even though they need symbols from
    * libglapi, in some version of Mesa they are not linked to it. Since
    * only the gl-renderer module links to it, the call above won't make
    * these symbols globally available, and loading the DRI driver fails.
    * Workaround this by dlopen()'ing libglapi with RTLD_GLOBAL. */
   dlopen("libglapi.so.0", RTLD_LAZY | RTLD_GLOBAL);

   if (!(gbm.device = gbm.api.gbm_create_device(drm.fd)))
      goto gbm_device_fail;

   struct wl_array infos;
   wl_array_init(&infos);

   if (!setup_drm(drm.fd, &infos))
      goto fail;

   struct drm_output_information *info;
   wl_array_for_each(info, &infos) {
      struct gbm_surface *surface;
      if (!(surface = gbm.api.gbm_surface_create(gbm.device, info->width, info->height, GBM_BO_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)))
         continue;

      add_output(compositor, gbm.device, surface, info);
   }

   wl_array_release(&infos);

   if (!(drm.event_source = wl_event_loop_add_fd(compositor->event_loop, drm.fd, WL_EVENT_READABLE, drm_event, NULL)))
      goto fail;

   if (!(seat.udev = wlc_udev_new(compositor)))
      goto fail;

   wlc_set_drm_control_functions(set_master, drop_master);
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
