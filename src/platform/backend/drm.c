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
} gbm;

static struct {
   int fd;
   struct wl_event_source *event_source;
} drm;

static void
release_fb(struct gbm_surface *surface, struct drm_fb *fb)
{
   assert(surface && fb);

   if (fb->fd > 0)
      drmModeRmFB(drm.fd, fb->fd);

   if (fb->bo)
      gbm_surface_release_buffer(surface, fb->bo);

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
   drmHandleEvent(fd, &evctx);
   return 0;
}

static bool
create_fb(struct gbm_surface *surface, struct drm_fb *fb)
{
   assert(surface && fb);

   if (!gbm_surface_has_free_buffers(surface))
      goto no_buffers;

   if (!(fb->bo = gbm_surface_lock_front_buffer(surface)))
      goto failed_to_lock;

   uint32_t width = gbm_bo_get_width(fb->bo);
   uint32_t height = gbm_bo_get_height(fb->bo);
   uint32_t handle = gbm_bo_get_handle(fb->bo).u32;
   uint32_t stride = gbm_bo_get_stride(fb->bo);

   if (drmModeAddFB(drm.fd, width, height, 24, 32, stride, handle, &fb->fd))
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
      if (drmModeSetCrtc(drm.fd, dsurface->crtc->crtc_id, fb->fd, 0, 0, &dsurface->connector->connector_id, 1, &dsurface->connector->modes[o->active.mode]))
         goto set_crtc_fail;

      dsurface->stride = fb->stride;
   }

   if (drmModePageFlip(drm.fd, dsurface->crtc->crtc_id, fb->fd, DRM_MODE_PAGE_FLIP_EVENT, bsurface))
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
      drmModeSetCrtc(drm.fd, dsurface->crtc->crtc_id, 0, 0, 0, NULL, 0, NULL);
      dsurface->stride = 0;
   }
}

static void
surface_release(struct wlc_backend_surface *bsurface)
{
   struct drm_surface *dsurface = bsurface->internal;
   struct drm_fb *fb = &dsurface->fb[dsurface->index];
   release_fb(dsurface->surface, fb);

   drmModeSetCrtc(drm.fd, dsurface->crtc->crtc_id, dsurface->crtc->buffer_id, dsurface->crtc->x, dsurface->crtc->y, &dsurface->connector->connector_id, 1, &dsurface->crtc->mode);

   if (dsurface->crtc)
      drmModeFreeCrtc(dsurface->crtc);

   if (dsurface->surface)
      gbm_surface_destroy(dsurface->surface);

   if (dsurface->encoder)
      drmModeFreeEncoder(dsurface->encoder);

   if (dsurface->connector)
      drmModeFreeConnector(dsurface->connector);

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
   drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoder_id);

   if (encoder) {
      *out_crtc_id = encoder->crtc_id;
      return encoder;
   } else {
      drmModeFreeEncoder(encoder);
      encoder = NULL;
   }

   for (int e = 0; e < resources->count_encoders; ++e) {
      if (!(encoder = drmModeGetEncoder(fd, resources->encoders[e])))
         continue;

      for (int c = 0; c < resources->count_crtcs; ++c) {
         if (!(encoder->possible_crtcs & (1 << c)))
            continue;

         *out_crtc_id = resources->crtcs[c];
         return encoder;
      }

      drmModeFreeEncoder(encoder);
   }

   return NULL;
}

static enum wlc_connector_type
wlc_connector_for_drm_connector(uint32_t type)
{
   switch (type) {
      case DRM_MODE_CONNECTOR_Unknown: return WLC_CONNECTOR_UNKNOWN;
      case DRM_MODE_CONNECTOR_VGA: return WLC_CONNECTOR_VGA;
      case DRM_MODE_CONNECTOR_DVII: return WLC_CONNECTOR_DVII;
      case DRM_MODE_CONNECTOR_DVID: return WLC_CONNECTOR_DVID;
      case DRM_MODE_CONNECTOR_DVIA: return WLC_CONNECTOR_DVIA;
      case DRM_MODE_CONNECTOR_Composite: return WLC_CONNECTOR_COMPOSITE;
      case DRM_MODE_CONNECTOR_SVIDEO: return WLC_CONNECTOR_SVIDEO;
      case DRM_MODE_CONNECTOR_LVDS: return WLC_CONNECTOR_LVDS;
      case DRM_MODE_CONNECTOR_Component: return WLC_CONNECTOR_COMPONENT;
      case DRM_MODE_CONNECTOR_9PinDIN: return WLC_CONNECTOR_DIN;
      case DRM_MODE_CONNECTOR_DisplayPort: return WLC_CONNECTOR_DP;
      case DRM_MODE_CONNECTOR_HDMIA: return WLC_CONNECTOR_HDMIA;
      case DRM_MODE_CONNECTOR_HDMIB: return WLC_CONNECTOR_HDMIB;
      case DRM_MODE_CONNECTOR_TV: return WLC_CONNECTOR_TV;
      case DRM_MODE_CONNECTOR_eDP: return WLC_CONNECTOR_eDP;
      case DRM_MODE_CONNECTOR_VIRTUAL: return WLC_CONNECTOR_VIRTUAL;
      case DRM_MODE_CONNECTOR_DSI: return WLC_CONNECTOR_DSI;
   }

   wlc_log(WLC_LOG_WARN, "Failed to resolve drm connector of type %u", type);
   return WLC_CONNECTOR_UNKNOWN;
}

static bool
query_drm(int fd, struct chck_iter_pool *out_infos)
{
   drmModeRes *resources;
   if (!(resources = drmModeGetResources(fd))) {
      wlc_log(WLC_LOG_WARN, "Failed to get drm resources");
      goto resources_fail;
   }

   for (int c = 0; c < resources->count_connectors; c++) {
      drmModeConnector *connector;
      if (!(connector = drmModeGetConnector(fd, resources->connectors[c]))) {
         wlc_log(WLC_LOG_WARN, "Failed to get connector %d", c);
         continue;
      }

      if (connector->connection != DRM_MODE_CONNECTED || connector->count_modes <= 0) {
         wlc_log(WLC_LOG_WARN, "Connector %d is not connected or has no modes", c);
         drmModeFreeConnector(connector);
         continue;
      }

      int32_t crtc_id;
      drmModeEncoder *encoder;
      if (!(encoder = find_encoder_for_connector(fd, resources, connector, &crtc_id))) {
         wlc_log(WLC_LOG_WARN, "Failed to find encoder for connector %d", c);
         drmModeFreeConnector(connector);
         continue;
      }

      drmModeCrtc *crtc;
      if (!(crtc = drmModeGetCrtc(drm.fd, crtc_id))) {
         wlc_log(WLC_LOG_WARN, "Failed to get crtc for connector %d (with id: %d)", c, crtc_id);
         drmModeFreeEncoder(encoder);
         drmModeFreeConnector(connector);
         continue;
      }

      struct drm_output_information *info;
      if (!(info = chck_iter_pool_push_back(out_infos, NULL))) {
         drmModeFreeCrtc(crtc);
         drmModeFreeEncoder(encoder);
         drmModeFreeConnector(connector);
         continue;
      }

      wlc_output_information(&info->info);
      chck_string_set_cstr(&info->info.make, "drm", false); // we can use colord for real info
      chck_string_set_cstr(&info->info.model, "unknown", false); // ^
      info->info.physical_width = connector->mmWidth;
      info->info.physical_height = connector->mmHeight;
      info->info.subpixel = connector->subpixel;
      info->info.scale = 1; // weston gets this from config?
      info->info.connector_id = connector->connector_type_id;
      info->info.connector = wlc_connector_for_drm_connector(connector->connector_type);

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

         wlc_log(WLC_LOG_INFO, "MODE: (%d) %ux%u@%u %s", c, mode.width, mode.height, mode.refresh, (mode.flags & WL_OUTPUT_MODE_CURRENT ? "*" : (mode.flags & WL_OUTPUT_MODE_PREFERRED ? "!" : "")));
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
      gbm_device_destroy(gbm.device);

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
      if (!(surface = gbm_surface_create(gbm.device, info->width, info->height, GBM_BO_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)))
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

   if (!(gbm.device = gbm_create_device(drm.fd)))
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
