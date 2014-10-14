#define _POSIX_C_SOURCE 200809L
#include "drm.h"
#include "wlc_internal.h"
#include "udev/udev.h"
#include "backend.h"
#include "compositor/compositor.h"
#include "compositor/output.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
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

struct drm_output_information {
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   struct wlc_output_information info;
   uint32_t width, height;
};

struct drm_output {
   struct gbm_device *device;
   struct gbm_surface *surface;
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   uint32_t stride;

   struct {
      uint32_t current_fb_id, next_fb_id;
      struct gbm_bo *current_bo, *next_bo;
   } flipper;
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
      fprintf(stderr, "-!- %s\n", dlerror());
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
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   return false;
}

static bool
drm_load(void)
{
   const char *lib = "libdrm.so", *func = NULL;

   if (!(drm.api.handle = dlopen(lib, RTLD_LAZY))) {
      fprintf(stderr, "-!- %s\n", dlerror());
      return false;
   }

#define load(x) (drm.api.x = dlsym(drm.api.handle, (func = #x)))

   if (!load(drmSetMaster))
      goto function_pointer_exception;
   if (!load(drmDropMaster))
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
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   return false;
}

static void
page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data)
{
   (void)frame, (void)sec, (void)usec;

   struct wlc_output *output = data;
   assert(output->backend_info);
   struct drm_output *drmo = output->backend_info;

   if (drmo->flipper.current_fb_id > 0)
      drm.api.drmModeRmFB(fd, drmo->flipper.current_fb_id);

   drmo->flipper.current_fb_id = drmo->flipper.next_fb_id;
   drmo->flipper.next_fb_id = 0;

   if (drmo->flipper.current_bo)
      gbm.api.gbm_surface_release_buffer(drmo->surface, drmo->flipper.current_bo);

   drmo->flipper.current_bo = drmo->flipper.next_bo;
   drmo->flipper.next_bo = NULL;
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
page_flip(struct wlc_output *output)
{
   assert(output->backend_info);
   struct drm_output *drmo = output->backend_info;

   /**
    * This is currently OpenGL specific (gbm).
    * TODO: vblanks etc..
    */

   if (drmo->flipper.next_bo || drmo->flipper.next_fb_id > 0) {
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(drm.fd, &rfds);

      /* both buffers busy, wait for them to finish */
      while (select(drm.fd + 1, &rfds, NULL, NULL, NULL) == -1);
      drm_event(drm.fd, 0, output);
   }

   if (!gbm.api.gbm_surface_has_free_buffers(drmo->surface))
      goto no_buffers;

   if (!(drmo->flipper.next_bo = gbm.api.gbm_surface_lock_front_buffer(drmo->surface)))
      goto failed_to_lock;

   uint32_t width = gbm.api.gbm_bo_get_width(drmo->flipper.next_bo);
   uint32_t height = gbm.api.gbm_bo_get_height(drmo->flipper.next_bo);
   uint32_t handle = gbm.api.gbm_bo_get_handle(drmo->flipper.next_bo).u32;
   uint32_t stride = gbm.api.gbm_bo_get_stride(drmo->flipper.next_bo);

   if (drm.api.drmModeAddFB(drm.fd, width, height, 24, 32, stride, handle, &drmo->flipper.next_fb_id))
      goto failed_to_create_fb;

   if (!drmo->flipper.current_bo || stride != drmo->stride) {
      if (drm.api.drmModeSetCrtc(drm.fd, drmo->encoder->crtc_id, drmo->flipper.next_fb_id, 0, 0, &drmo->connector->connector_id, 1, &drmo->connector->modes[output->mode]))
         goto set_crtc_fail;
   }

   drmo->stride = stride;

   if (drm.api.drmModePageFlip(drm.fd, drmo->encoder->crtc_id, drmo->flipper.next_fb_id, DRM_MODE_PAGE_FLIP_EVENT, output))
      goto failed_to_page_flip;

   return true;

no_buffers:
   fprintf(stderr, "gbm is out of buffers\n");
   goto fail;
failed_to_lock:
   fprintf(stderr, "failed to lock front buffer\n");
   goto fail;
failed_to_create_fb:
   fprintf(stderr, "failed to create fb\n");
   goto fail;
set_crtc_fail:
   fprintf(stderr, "failed to set mode\n");
   goto fail;
failed_to_page_flip:
   fprintf(stderr, "failed to page flip: %m\n");
fail:
   if (drmo->flipper.next_fb_id > 0) {
      drm.api.drmModeRmFB(drm.fd, drmo->flipper.next_fb_id);
      drmo->flipper.next_fb_id = 0;
   }
   if (drmo->flipper.next_bo) {
      gbm.api.gbm_surface_release_buffer(drmo->surface, drmo->flipper.next_bo);
      drmo->flipper.next_bo = NULL;
   }
   return false;
}

static EGLNativeDisplayType
get_display(struct wlc_output *output)
{
#if 0
   assert(output->backend_info);
   struct drm_output *drmo = output->backend_info;
   return (EGLNativeDisplayType)drmo->device;
#else
   (void)output;
   return (EGLNativeDisplayType)gbm.device;
#endif
}

static EGLNativeWindowType
get_window(struct wlc_output *output)
{
   assert(output->backend_info);
   struct drm_output *drmo = output->backend_info;
   return (EGLNativeWindowType)drmo->surface;
}

static bool
add_output(struct wlc_compositor *compositor, struct gbm_device *device, struct gbm_surface *surface, struct drm_output_information *info)
{
   struct drm_output *drmo = NULL;
   struct wlc_output *output = NULL;

   if (!(output = wlc_output_new(compositor, &info->info)))
      goto fail;

   if (!(output->backend_info = drmo = calloc(1, sizeof(struct drm_output))))
      goto fail;

   drmo->connector = info->connector;
   drmo->encoder = info->encoder;
   drmo->surface = surface;
   drmo->device = device;

   if (!compositor->api.add_output(compositor, output))
      goto fail;

   return true;

fail:
   gbm.api.gbm_surface_destroy(surface);
   drm.api.drmModeFreeEncoder(info->encoder);
   drm.api.drmModeFreeConnector(info->connector);
   if (output)
      wlc_output_free(output);
   if (drmo)
      free(drmo);
   return false;
}

static int
remove_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   assert(output->backend_info);
   struct drm_output *drmo = output->backend_info;
   compositor->api.remove_output(compositor, output);

   if (drmo->surface)
      gbm.api.gbm_surface_destroy(drmo->surface);

   if (drmo->encoder)
      drm.api.drmModeFreeEncoder(drmo->encoder);

   if (drmo->connector)
      drm.api.drmModeFreeConnector(drmo->connector);

   free(drmo);
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

   drmModeModeInfo current_mode;
   memset(&current_mode, 0, sizeof(current_mode));

   for (int i = 0; i < resources->count_connectors; i++) {
      drmModeConnector *connector;
      if (!(connector = drm.api.drmModeGetConnector(fd, resources->connectors[i])))
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

      if (crtc->mode_valid)
         memcpy(&current_mode, &crtc->mode, sizeof(current_mode));

      for (int i = 0; i < connector->count_modes; ++i) {
         struct wlc_output_mode mode;
         memset(&mode, 0, sizeof(mode));
         mode.refresh = connector->modes[i].vrefresh;
         mode.width = connector->modes[i].hdisplay;
         mode.height = connector->modes[i].vdisplay;

         if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED)
            mode.flags |= WL_OUTPUT_MODE_PREFERRED;

         if (!memcmp(&connector->modes[i], &current_mode, sizeof(current_mode))) {
            mode.flags |= WL_OUTPUT_MODE_CURRENT;
            info->width = connector->modes[i].hdisplay;
            info->height = connector->modes[i].vdisplay;
         }

         wlc_output_information_add_mode(&info->info, &mode);
      }

      drm.api.drmModeFreeCrtc(crtc);
      info->encoder = encoder;
      info->connector = connector;
   }

   return true;

resources_fail:
   fprintf(stderr, "drmModeGetResources failed\n");
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
   if (!gbm_load())
      goto fail;

   if (!drm_load())
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

   out_backend->name = "drm";
   out_backend->terminate = terminate;
   out_backend->api.display = get_display;
   out_backend->api.window = get_window;
   out_backend->api.page_flip = page_flip;
   return true;

card_open_fail:
   fprintf(stderr, "failed to open card: /dev/dri/card0\n");
   goto fail;
gbm_device_fail:
   fprintf(stderr, "gbm_create_device failed\n");
fail:
   terminate();
   return false;
}
