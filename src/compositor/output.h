#ifndef _WLC_OUTPUT_H_
#define _WLC_OUTPUT_H_

#include <stdint.h>
#include <wayland-util.h>
#include <chck/string/string.h>
#include <chck/pool/pool.h>
#include "platform/backend/backend.h"
#include "platform/context/context.h"
#include "platform/render/render.h"
#include "resources/resources.h"
#include "internal.h"

struct wl_global;
struct wlc_surface;
struct wlc_buffer;
struct timespec;

enum output_link {
   LINK_BELOW,
   LINK_ABOVE,
};

struct wlc_output_information {
   struct chck_iter_pool modes;
   struct chck_string name, make, model;
   int32_t x, y;
   int32_t physical_width, physical_height;
   int32_t subpixel;
   uint32_t connector_id;
   uint32_t crtc_id;
   enum wl_output_transform transform;
   enum wlc_connector_type connector;
};

struct wlc_output {
   struct wlc_source resources;
   struct wlc_size mode, resolution, virtual;
   struct wlc_output_information information;
   struct wlc_backend_surface bsurface;
   struct wlc_context context;
   struct wlc_render render;

   // XXX: maybe we can use source later and provide move semantics (for views)?
   struct chck_iter_pool surfaces, views, mutable;
   struct chck_iter_pool callbacks, visible;

   // Pixel blit buffer size of current resolution
   // Used to do visibility checks
   bool *blit;

   // Scale of the output
   // Affects virtual resolution by dividing with the scale
   uint32_t scale;

   struct {
      struct wl_event_source *idle;
   } timer;

   struct {
      struct wl_global *output;
   } wl;

   // FIXME: replace with better system
   struct {
      struct wlc_backend_surface bsurface;
      bool terminate;
      bool sleep;
   } task;

   struct {
      float ims;
      uint32_t frame_time;
      bool pending, scheduled, activity, sleeping;
      bool background_visible;
      bool created;
   } state;

   struct {
      uint32_t mode;
      uint32_t mask;
   } active;
};

WLC_NONULL bool wlc_output_information(struct wlc_output_information *info);
void wlc_output_information_release(struct wlc_output_information *info);
WLC_NONULL bool wlc_output_information_add_mode(struct wlc_output_information *info, struct wlc_output_mode *mode);

WLC_NONULLV(2) void wlc_output_finish_frame(struct wlc_output *output, const struct timespec *ts);
void wlc_output_schedule_repaint(struct wlc_output *output);
WLC_NONULLV(2) bool wlc_output_surface_attach(struct wlc_output *output, struct wlc_surface *surface, struct wlc_buffer *buffer);
WLC_NONULLV(2) void wlc_output_surface_destroy(struct wlc_output *output, struct wlc_surface *surface);
bool wlc_output_set_backend_surface(struct wlc_output *output, struct wlc_backend_surface *surface);
void wlc_output_set_information(struct wlc_output *output, struct wlc_output_information *info);
WLC_NONULLV(2) void wlc_output_unlink_view(struct wlc_output *output, struct wlc_view *view);
WLC_NONULLV(2) void wlc_output_link_view(struct wlc_output *output, struct wlc_view *view, enum output_link link, struct wlc_view *other);
void wlc_output_terminate(struct wlc_output *output);
void wlc_output_release(struct wlc_output *output);
WLC_NONULL bool wlc_output(struct wlc_output *output);

void wlc_output_focus_ptr(struct wlc_output *output);
void wlc_output_set_sleep_ptr(struct wlc_output *output, bool sleep);
void wlc_output_set_gamma_ptr(struct wlc_output *output, uint16_t size, uint16_t *r, uint16_t *g, uint16_t *b);
WLC_NONULLV(2) bool wlc_output_set_resolution_ptr(struct wlc_output *output, const struct wlc_size *resolution, uint32_t scale);
void wlc_output_set_mask_ptr(struct wlc_output *output, uint32_t mask);
WLC_NONULLV(2) void wlc_output_get_pixels_ptr(struct wlc_output *output, bool (*pixels)(const struct wlc_size *size, uint8_t *rgba, void *arg), void *arg);
bool wlc_output_set_views_ptr(struct wlc_output *output, const wlc_handle *views, size_t memb);
const wlc_handle* wlc_output_get_views_ptr(struct wlc_output *output, size_t *out_memb);
const wlc_handle* wlc_output_get_modes_ptr(struct wlc_output *output, size_t *out_memb);
wlc_handle* wlc_output_get_mutable_views_ptr(struct wlc_output *output, size_t *out_memb);

/** for wlc-render.h */
WLC_NONULL void wlc_output_render_surface(struct wlc_output *output, struct wlc_surface *surface, const struct wlc_geometry *geometry, struct chck_iter_pool *callbacks);
struct wlc_output* wlc_get_rendering_output(void);

#endif /* _WLC_OUTPUT_H_ */
