#define _POSIX_C_SOURCE 200809L
#include "drm.h"
#include "backend.h"

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

static struct {
   int fd;
   struct gbm_device *dev;
   struct gbm_surface *surface;

   struct {
      void *handle;

      struct gbm_device* (*gbm_create_device)(int);
      void (*gbm_device_destroy)(struct gbm_device*);
      struct gbm_surface* (*gbm_surface_create)(struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t);
      void (*gbm_surface_destroy)(struct gbm_surface*);
      uint32_t (*gbm_bo_get_stride)(struct gbm_bo*);
      union gbm_bo_handle (*gbm_bo_get_handle)(struct gbm_bo*);
      int (*gbm_surface_has_free_buffers)(struct gbm_surface*);
      struct gbm_bo* (*gbm_surface_lock_front_buffer)(struct gbm_surface*);
      int (*gbm_surface_release_buffer)(struct gbm_surface*, struct gbm_bo*);
   } api;
} gbm;

static struct {
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   drmModeModeInfo mode;

   struct {
      void *handle;

      int (*drmModeAddFB)(int, uint32_t, uint32_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t*);
      int (*drmModeRmFB)(int, uint32_t);
      int (*drmModePageFlip)(int, uint32_t, uint32_t, uint32_t, void*);
      int (*drmHandleEvent)(int, drmEventContextPtr);
      drmModeResPtr (*drmModeGetResources)(int);
      drmModeConnectorPtr (*drmModeGetConnector)(int, uint32_t);
      void (*drmModeFreeConnector)(drmModeConnectorPtr);
      drmModeEncoderPtr (*drmModeGetEncoder)(int, uint32_t);
      void (*drmModeFreeEncoder)(drmModeEncoderPtr);
   } api;
} kms;

static struct {
   uint32_t current_fb_id, next_fb_id;
   struct gbm_bo *current_bo, *next_bo;
} flipper;

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

   if (!(kms.api.handle = dlopen(lib, RTLD_LAZY))) {
      fprintf(stderr, "-!- %s\n", dlerror());
      return false;
   }

#define load(x) (kms.api.x = dlsym(kms.api.handle, (func = #x)))

   if (!load(drmModeAddFB))
      goto function_pointer_exception;
   if (!load(drmModeRmFB))
      goto function_pointer_exception;
   if (!load(drmModePageFlip))
      goto function_pointer_exception;
   if (!load(drmHandleEvent))
      goto function_pointer_exception;
   if (!load(drmModeGetResources))
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
   (void)frame, (void)sec, (void)usec, (void)data;

   if (flipper.current_fb_id)
      kms.api.drmModeRmFB(fd, flipper.current_fb_id);

   flipper.current_fb_id = flipper.next_fb_id;
   flipper.next_fb_id = 0;

   if (flipper.current_bo)
      gbm.api.gbm_surface_release_buffer(gbm.surface, flipper.current_bo);

   flipper.current_bo = flipper.next_bo;
   flipper.next_bo = NULL;
}

static void
page_flip(void)
{
   if (!gbm.api.gbm_surface_has_free_buffers(gbm.surface))
      goto no_buffers;

   if (!(flipper.next_bo = gbm.api.gbm_surface_lock_front_buffer(gbm.surface)))
      goto failed_to_lock;

   uint32_t handle = gbm.api.gbm_bo_get_handle(flipper.next_bo).u32;
   uint32_t stride = gbm.api.gbm_bo_get_stride(flipper.next_bo);

   if (kms.api.drmModeAddFB(gbm.fd, kms.mode.hdisplay, kms.mode.vdisplay, 24, 32, stride, handle, &flipper.next_fb_id))
      goto failed_to_create_fb;

   if (kms.api.drmModePageFlip(gbm.fd, kms.encoder->crtc_id, flipper.next_fb_id, DRM_MODE_PAGE_FLIP_EVENT, 0))
      goto failed_to_page_flip;

   fd_set rfds;
   FD_ZERO(&rfds);
   FD_SET(gbm.fd, &rfds);
   while (select(gbm.fd + 1, &rfds, NULL, NULL, NULL) == -1);

   drmEventContext evctx;
   memset(&evctx, 0, sizeof(evctx));
   evctx.version = DRM_EVENT_CONTEXT_VERSION;
   evctx.page_flip_handler = page_flip_handler;
   kms.api.drmHandleEvent(gbm.fd, &evctx);
   return;

no_buffers:
   fprintf(stderr, "gbm is out of buffers\n");
   return;
failed_to_lock:
   fprintf(stderr, "failed to lock front buffer: %m\n");
   return;
failed_to_create_fb:
   fprintf(stderr, "failed to create fb\n");
   return;
failed_to_page_flip:
   fprintf(stderr, "failed to page flip: %m\n");
}

static EGLNativeDisplayType
get_display(void)
{
   return (EGLNativeDisplayType)gbm.dev;
}

static EGLNativeWindowType
get_window(void)
{
   return (EGLNativeWindowType)gbm.surface;
}

static int
poll_events(struct wlc_seat *seat)
{
   (void)seat;
   return 0;
}

static bool
setup_kms(int fd)
{
   drmModeRes *resources;
   if (!(resources = kms.api.drmModeGetResources(fd)))
      goto resources_fail;

   drmModeConnector *connector = NULL;
   for (int i = 0; i < resources->count_connectors; i++) {
      if (!(connector = kms.api.drmModeGetConnector(fd, resources->connectors[i])))
         continue;

      if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0)
         break;

      kms.api.drmModeFreeConnector(connector);
      connector = NULL;
   }

   if (!connector)
      goto connector_not_found;

   drmModeEncoder *encoder = NULL;
   for (int i = 0; i < resources->count_encoders; i++) {
      if (!(encoder = kms.api.drmModeGetEncoder(fd, resources->encoders[i])))
         continue;

      if (encoder->encoder_id == connector->encoder_id)
         break;

      kms.api.drmModeFreeEncoder(encoder);
      encoder = NULL;
   }

   if (!encoder)
      goto encoder_not_found;

   kms.connector = connector;
   kms.encoder = encoder;
   kms.mode = connector->modes[0];
   return true;

resources_fail:
   fprintf(stderr, "drmModeGetResources failed\n");
   goto fail;
connector_not_found:
   fprintf(stderr, "Could not find active connector\n");
   goto fail;
encoder_not_found:
   fprintf(stderr, "Could not find active encoder\n");
fail:
   memset(&kms, 0, sizeof(kms));
   return false;
}

static void
terminate(void)
{
   if (gbm.surface)
      gbm.api.gbm_surface_destroy(gbm.surface);

   if (gbm.dev)
      gbm.api.gbm_device_destroy(gbm.dev);

   if (kms.api.handle)
      dlclose(kms.api.handle);

   if (gbm.api.handle)
      dlclose(gbm.api.handle);

   memset(&kms, 0, sizeof(kms));
   memset(&gbm, 0, sizeof(gbm));
}

bool
wlc_drm_init(struct wlc_backend *out_backend)
{
   if (!gbm_load())
      goto fail;

   if (!drm_load())
      goto fail;

   if ((gbm.fd = open("/dev/dri/card0", O_RDWR)) < 0)
      goto card_open_fail;

   if (!(gbm.dev = gbm.api.gbm_create_device(gbm.fd)))
      goto gbm_device_fail;

   if (!setup_kms(gbm.fd))
      goto fail;

   if (!(gbm.surface = gbm.api.gbm_surface_create(gbm.dev, kms.mode.hdisplay, kms.mode.vdisplay, GBM_BO_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)))
      goto gbm_surface_fail;

   out_backend->name = "drm";
   out_backend->terminate = terminate;
   out_backend->api.display = get_display;
   out_backend->api.window = get_window;
   out_backend->api.poll_events = poll_events;
   out_backend->api.page_flip = page_flip;
   return true;

card_open_fail:
   fprintf(stderr, "failed to open card: /dev/dri/card0\n");
   goto fail;
gbm_device_fail:
   fprintf(stderr, "gbm_create_device failed\n");
   goto fail;
gbm_surface_fail:
   fprintf(stderr, "gbm_surface_creat failed\n");
fail:
   terminate();
   return false;
}
