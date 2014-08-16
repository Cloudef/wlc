#include "region.h"
#include "macros.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_region_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   wl_resource_destroy(resource);
}

static void
wl_cb_region_add(struct wl_client *wl_client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)wl_client;
   struct wlc_region *region = wl_resource_get_user_data(resource);
   pixman_region32_union_rect(&region->region, &region->region, x, y, width, height);
}

static void
wl_cb_region_substract(struct wl_client *wl_client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)wl_client;
   struct wlc_region *region = wl_resource_get_user_data(resource);

   pixman_region32_t rect;
   pixman_region32_init_rect(&rect, x, y, width, height);
   pixman_region32_subtract(&region->region, &region->region, &rect);
   pixman_region32_fini(&rect);
}

static const struct wl_region_interface wl_region_implementation = {
   wl_cb_region_destroy,
   wl_cb_region_add,
   wl_cb_region_substract,
};

static void
wl_cb_region_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wlc_region *region = wl_resource_get_user_data(resource);

   if (region)
      wlc_region_free(region);
}

void
wlc_region_implement(struct wlc_region *region, struct wl_resource *resource)
{
   assert(region);

   if (region->resource == resource)
      return;

   if (region->resource)
      wl_resource_destroy(region->resource);

   region->resource = resource;
   wl_resource_set_implementation(region->resource, &wl_region_implementation, region, wl_cb_region_destructor);
}

void
wlc_region_free(struct wlc_region *region)
{
   assert(region);
   pixman_region32_fini(&region->region);
   free(region);
}

struct wlc_region*
wlc_region_new(void)
{
   struct wlc_region *region;
   if (!(region = calloc(1, sizeof(struct wlc_region))))
      return NULL;

   return region;
}
