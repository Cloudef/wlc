#ifndef _WLC_OUTPUT_H_
#define _WLC_OUTPUT_H_

#include <stdint.h>
#include <wayland-util.h>

struct wl_global;
struct wlc_compositor;

struct wlc_output {
   struct wl_global *global;
   uint32_t physical_width, physical_height;
   uint32_t connector;
   struct wl_list resources;
};

struct wlc_output* wlc_output_new(struct wlc_compositor *compositor);
void wlc_output_free(struct wlc_output *output);

#endif /* _WLC_OUTPUT_H_ */
