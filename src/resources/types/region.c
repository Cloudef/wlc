#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <wayland-server.h>
#include "region.h"
#include "macros.h"
#include "resources/resources.h"

static void
wl_cb_region_add(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)client;

   struct wlc_region *region;
   if (!(region = convert_from_wl_resource(resource, "region")))
      return;

   pixman_region32_union_rect(&region->region, &region->region, x, y, width, height);
}

static void
wl_cb_region_subtract(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)client;

   struct wlc_region *region;
   if (!(region = convert_from_wl_resource(resource, "region")))
      return;

   pixman_region32_t rect;
   pixman_region32_init_rect(&rect, x, y, width, height);
   pixman_region32_subtract(&region->region, &region->region, &rect);
   pixman_region32_fini(&rect);
}

const struct wl_region_interface wl_region_implementation = {
   .destroy = wlc_cb_resource_destructor,
   .add = wl_cb_region_add,
   .subtract = wl_cb_region_subtract,
};

void
wlc_region_release(struct wlc_region *region)
{
   if (!region)
      return;

   pixman_region32_fini(&region->region);
}
