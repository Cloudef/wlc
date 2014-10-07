#ifndef _WLC_OUTPUT_H_
#define _WLC_OUTPUT_H_

#include <stdint.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include "types/string.h"

struct wl_global;
struct wlc_compositor;

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
   void *backend_info;
   void *context_info;
   void *render_info;
   struct wl_global *global;
   struct wlc_output_information information;
   struct wl_list resources;
   struct wl_list link;
};

bool wlc_output_information_add_mode(struct wlc_output_information *info, struct wlc_output_mode *mode);
struct wlc_output* wlc_output_new(struct wlc_compositor *compositor, struct wlc_output_information *info);
void wlc_output_free(struct wlc_output *output);

#endif /* _WLC_OUTPUT_H_ */
