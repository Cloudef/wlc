#ifndef _WLC_OUTPUT_H_
#define _WLC_OUTPUT_H_

#include <stdint.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include "types/string.h"
#include "types/geometry.h"

struct wl_global;
struct wlc_backend_surface;
struct wlc_compositor;
struct wlc_surface;
struct wlc_buffer;
struct timespec;

struct wlc_space {
   void *userdata;
   struct wlc_output *output;
   struct wl_list views;
   struct wl_list link;
};

struct wlc_output_mode {
   int32_t refresh;
   int32_t width, height;
   uint32_t flags; // WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED
};

struct wlc_output_information {
   struct wlc_string make, model;
   int32_t x, y;
   int32_t physical_width, physical_height;
   int32_t subpixel;
   int32_t scale;
   enum wl_output_transform transform;
   struct wl_array modes;
};

struct wlc_output {
   void *userdata;
   struct wlc_backend_surface *surface;
   struct wlc_compositor *compositor;
   struct wlc_context *context;
   struct wlc_render *render;
   struct wlc_space *space;
   struct wl_global *global;
   struct wl_event_source *idle_timer;
   struct wlc_output_information information;
   struct wlc_size resolution;
   struct wl_list resources, spaces;
   struct wl_list link;

   struct {
      void (*pixels)(uint32_t w, uint32_t h, uint8_t *rgba);
   } task;

   float ims;
   uint32_t frame_time;
   uint32_t mode;

   bool pending, scheduled, activity, terminating;
   bool background_visible;
};

void wlc_output_finish_frame(struct wlc_output *output, const struct timespec *ts);
void wlc_output_schedule_repaint(struct wlc_output *output);
bool wlc_output_information_add_mode(struct wlc_output_information *info, struct wlc_output_mode *mode);
bool wlc_output_surface_attach(struct wlc_output *output, struct wlc_surface *surface, struct wlc_buffer *buffer);
void wlc_output_surface_destroy(struct wlc_output *output, struct wlc_surface *surface);
bool wlc_output_set_surface(struct wlc_output *output, struct wlc_backend_surface *surface);
struct wlc_output* wlc_output_new(struct wlc_compositor *compositor, struct wlc_backend_surface *surface, struct wlc_output_information *info);
void wlc_output_terminate(struct wlc_output *output);
void wlc_output_free(struct wlc_output *output);

#endif /* _WLC_OUTPUT_H_ */
