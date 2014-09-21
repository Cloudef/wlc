#define _POSIX_C_SOURCE 200809L
#include "drm.h"
#include "udev/udev.h"
#include "backend.h"
#include "compositor/compositor.h"

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
   struct wl_event_source *event_source;

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
} drm;

static struct {
   uint32_t current_fb_id, next_fb_id;
   struct gbm_bo *current_bo, *next_bo;
} flipper;

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
      drm.api.drmModeRmFB(fd, flipper.current_fb_id);

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

   if (drm.api.drmModeAddFB(gbm.fd, drm.mode.hdisplay, drm.mode.vdisplay, 24, 32, stride, handle, &flipper.next_fb_id))
      goto failed_to_create_fb;

   if (drm.api.drmModePageFlip(gbm.fd, drm.encoder->crtc_id, flipper.next_fb_id, DRM_MODE_PAGE_FLIP_EVENT, 0))
      fprintf(stderr, "failed to page flip: %m\n");

   fd_set rfds;
   FD_ZERO(&rfds);
   FD_SET(gbm.fd, &rfds);
   while (select(gbm.fd + 1, &rfds, NULL, NULL, NULL) == -1);

   drmEventContext evctx;
   memset(&evctx, 0, sizeof(evctx));
   evctx.version = DRM_EVENT_CONTEXT_VERSION;
   evctx.page_flip_handler = page_flip_handler;
   drm.api.drmHandleEvent(gbm.fd, &evctx);
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
}

static void
vblank_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data)
{
   (void)fd, (void)frame, (void)sec, (void)usec, (void)data;
   // STUB
}

static int
drm_event(int fd, uint32_t mask, void *data)
{
   (void)mask, (void)data;
   drmEventContext evctx;
   memset(&evctx, 0, sizeof(evctx));
   evctx.version = DRM_EVENT_CONTEXT_VERSION;
   evctx.page_flip_handler = page_flip_handler;
   evctx.vblank_handler = vblank_handler;
   drm.api.drmHandleEvent(fd, &evctx);
   return 1;
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

static bool
setup_drm(int fd)
{
   drmModeRes *resources;
   if (!(resources = drm.api.drmModeGetResources(fd)))
      goto resources_fail;

   drmModeConnector *connector = NULL;
   for (int i = 0; i < resources->count_connectors; i++) {
      if (!(connector = drm.api.drmModeGetConnector(fd, resources->connectors[i])))
         continue;

      if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0)
         break;

      drm.api.drmModeFreeConnector(connector);
      connector = NULL;
   }

   if (!connector)
      goto connector_not_found;

   drmModeEncoder *encoder = NULL;
   for (int i = 0; i < resources->count_encoders; i++) {
      if (!(encoder = drm.api.drmModeGetEncoder(fd, resources->encoders[i])))
         continue;

      if (encoder->encoder_id == connector->encoder_id)
         break;

      drm.api.drmModeFreeEncoder(encoder);
      encoder = NULL;
   }

   if (!encoder)
      goto encoder_not_found;

   drm.connector = connector;
   drm.encoder = encoder;
   drm.mode = connector->modes[0];

   for (int i = 0; i < connector->count_modes; ++i)
      printf("%d. %dx%d\n", i, connector->modes[i].hdisplay, connector->modes[i].vdisplay);

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
   memset(&drm, 0, sizeof(drm));
   return false;
}

static void
terminate(void)
{
   if (drm.event_source)
      wl_event_source_remove(drm.event_source);

   if (gbm.surface)
      gbm.api.gbm_surface_destroy(gbm.surface);

   if (gbm.dev)
      gbm.api.gbm_device_destroy(gbm.dev);

   if (drm.api.handle)
      dlclose(drm.api.handle);

   if (gbm.api.handle)
      dlclose(gbm.api.handle);

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

   if ((gbm.fd = open("/dev/dri/card0", O_RDWR)) < 0)
      goto card_open_fail;

   dlopen("libglapi.so.0", RTLD_LAZY | RTLD_GLOBAL);

   if (!(gbm.dev = gbm.api.gbm_create_device(gbm.fd)))
      goto gbm_device_fail;

   if (!setup_drm(gbm.fd))
      goto fail;

   if (!(gbm.surface = gbm.api.gbm_surface_create(gbm.dev, drm.mode.hdisplay, drm.mode.vdisplay, GBM_BO_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)))
      goto gbm_surface_fail;

#if 0
   if (!(drm.event_source = wl_event_loop_add_fd(compositor->event_loop, drm.fd, WL_EVENT_READABLE, drm_event, NULL)))
      goto event_source_fail;
#endif

   if (!(seat.udev = wlc_udev_new(compositor)))
      goto fail;

   compositor->api.resolution(compositor, drm.mode.hdisplay, drm.mode.vdisplay);

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
   goto fail;
gbm_surface_fail:
   fprintf(stderr, "gbm_surface_creat failed\n");
#if 0
event_source_fail:
   fprintf(stderr, "-!- failed to add DRM event source\n");
#endif
fail:
   terminate();
   return false;
}
