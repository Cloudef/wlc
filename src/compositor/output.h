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

struct wl_global;
struct wlc_surface;
struct wlc_buffer;
struct timespec;

enum output_link {
   LINK_BELOW,
   LINK_ABOVE,
};

struct wlc_output_mode {
   int32_t refresh;
   int32_t width, height;
   uint32_t flags; // WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED
};

struct wlc_output_information {
   struct chck_iter_pool modes;
   struct chck_string make, model;
   int32_t x, y;
   int32_t physical_width, physical_height;
   int32_t subpixel;
   int32_t scale;
   enum wl_output_transform transform;
};

struct wlc_output {
   struct wlc_source resources;
   struct wlc_size resolution;
   struct wlc_output_information information;
   struct wlc_backend_surface bsurface;
   struct wlc_context context;
   struct wlc_render render;

   // XXX: maybe we can use source later and provide move semantics (for views)?
   struct chck_iter_pool surfaces, views, mutable;

   struct {
      struct wl_event_source *idle, *sleep;
   } timer;

   struct {
      struct wl_global *output;
   } wl;

   struct {
      struct {
         void *arg;
         void (*cb)(const struct wlc_size *size, uint8_t *rgba, void *userdata);
      } pixels;
      struct wlc_backend_surface *bsurface;
      bool terminate;
      bool sleep;
   } task;

   struct {
      float ims;
      uint32_t frame_time;
      bool pending, scheduled, activity, sleeping;
      bool background_visible;
   } state;

   struct {
      uint32_t mode;
      uint32_t mask;
   } active;

   struct {
      uint32_t idle_time;
      bool enable_bg;
   } options;
};

bool wlc_output_information(struct wlc_output_information *info);
void wlc_output_information_release(struct wlc_output_information *info);
bool wlc_output_information_add_mode(struct wlc_output_information *info, struct wlc_output_mode *mode);

void wlc_output_finish_frame(struct wlc_output *output, const struct timespec *ts);
void wlc_output_schedule_repaint(struct wlc_output *output);
bool wlc_output_surface_attach(struct wlc_output *output, struct wlc_surface *surface, struct wlc_buffer *buffer);
void wlc_output_surface_destroy(struct wlc_output *output, struct wlc_surface *surface);
bool wlc_output_set_backend_surface(struct wlc_output *output, struct wlc_backend_surface *surface);
void wlc_output_set_information(struct wlc_output *output, struct wlc_output_information *info);
void wlc_output_unlink_view(struct wlc_output *output, struct wlc_view *view);
void wlc_output_link_view(struct wlc_output *output, struct wlc_view *view, enum output_link link, struct wlc_view *other);
void wlc_output_terminate(struct wlc_output *output);
void wlc_output_release(struct wlc_output *output);
bool wlc_output(struct wlc_output *output);

void wlc_output_set_sleep_ptr(struct wlc_output *output, bool sleep);
void wlc_output_set_resolution_ptr(struct wlc_output *output, const struct wlc_size *resolution);
void wlc_output_set_mask_ptr(struct wlc_output *output, uint32_t mask);
void wlc_output_get_pixels_ptr(struct wlc_output *output, void (*async)(const struct wlc_size *size, uint8_t *rgba, void *userdata), void *userdata);
bool wlc_output_set_views_ptr(struct wlc_output *output, const wlc_handle *views, size_t memb);
const wlc_handle* wlc_output_get_views_ptr(struct wlc_output *output, size_t *out_memb);


#endif /* _WLC_OUTPUT_H_ */
