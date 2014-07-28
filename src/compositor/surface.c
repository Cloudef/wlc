#include "surface.h"
#include "compositor.h"
#include "region.h"
#include "buffer.h"
#include "callback.h"
#include "macros.h"

#include "render/render.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <wayland-server.h>

static void
wlc_surface_state_set_buffer(struct wlc_surface_state *state, struct wlc_buffer *buffer)
{
   if (state->buffer == buffer)
      return;

   state->buffer = buffer;
}

static void
wlc_surface_set_size(struct wlc_surface *surface, int32_t width, int32_t height)
{
   if (surface->width == width && surface->height == height)
      return;

   surface->width = width;
   surface->height = height;
}

static void
wlc_surface_update_size(struct wlc_surface *surface)
{
   int32_t width = surface->width_from_buffer;
   int32_t height = surface->height_from_buffer;
   wlc_surface_set_size(surface, width, height);
}

static void
wlc_surface_attach(struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   if (!buffer) {
      /* TODO: unmap surface if mapped */
   }

   surface->compositor->render->api.attach(surface, buffer);

   int32_t width = 0, height = 0;
   if (buffer) {
#if 0
      switch (transform) {
         case WL_OUTPUT_TRANSFORM_90:
         case WL_OUTPUT_TRANSFORM_270:
         case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            width = surface->buffer_ref.buffer->height / vp->buffer.scale;
            height = surface->buffer_ref.buffer->width / vp->buffer.scale;
            break;
         default:
            width = surface->buffer_ref.buffer->width / vp->buffer.scale;
            height = surface->buffer_ref.buffer->height / vp->buffer.scale;
            break;
      }
#endif
      width = buffer->width;
      height = buffer->height;
   }

   surface->width_from_buffer = width;
   surface->height_from_buffer = height;
}

static void
wlc_surface_commit_state(struct wlc_surface *surface, struct wlc_surface_state *state)
{
   if (state->newly_attached)
      wlc_surface_attach(surface, state->buffer);
   wlc_surface_state_set_buffer(state, NULL);

   if (state->newly_attached)
      wlc_surface_update_size(surface);

   state->x = 0;
   state->y = 0;
   state->newly_attached = false;

   pixman_region32_union(&surface->commit.damage, &surface->commit.damage, &state->damage);
   pixman_region32_intersect_rect(&surface->commit.damage, &surface->commit.damage, 0, 0, surface->width, surface->height);
   pixman_region32_clear(&surface->pending.damage);

   pixman_region32_t opaque;
   pixman_region32_init(&opaque);
   pixman_region32_intersect_rect(&opaque, &state->opaque, 0, 0, surface->width, surface->height);

   if (!pixman_region32_equal(&opaque, &surface->commit.opaque))
      pixman_region32_copy(&surface->commit.opaque, &opaque);

   pixman_region32_fini(&opaque);

   pixman_region32_intersect_rect(&surface->commit.input, &state->input, 0, 0, surface->width, surface->height);

   surface->compositor->api.schedule_repaint(surface->compositor);
}

static void
wl_cb_surface_destroy(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   STUBL(resource);
   wl_resource_destroy(resource);
}

static void
wl_cb_surface_attach(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer_resource, int32_t x, int32_t y)
{
   struct wlc_surface *surface = wl_resource_get_user_data(resource);
   struct wlc_buffer *buffer = NULL;

   if (buffer_resource) {
      if (!(buffer = wlc_buffer_new(buffer_resource))) {
         wl_client_post_no_memory(client);
         return;
      }
   }

   wlc_surface_state_set_buffer(&surface->pending, buffer);

   surface->pending.x = x;
   surface->pending.y = y;
   surface->pending.newly_attached = true;
}

static void
wl_cb_surface_damage(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);
   pixman_region32_union_rect(&surface->pending.damage, &surface->pending.damage, x, y, width, height);
}

static void
wl_cb_surface_frame(struct wl_client *client, struct wl_resource *resource, uint32_t callback_id)
{
   (void)client;
   struct wl_resource *callback_resource;
   if (!(callback_resource = wl_resource_create(client, &wl_callback_interface, 1, callback_id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   struct wlc_callback *callback;
   if (!(callback = wlc_callback_new(callback_resource))) {
      wl_resource_destroy(callback_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   wlc_callback_implement(callback);

   struct wlc_surface *surface = wl_resource_get_user_data(resource);
   surface->frame_cb = callback;
}

static void
wl_cb_surface_set_opaque_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region_resource)
{
   (void)client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   if (region_resource) {
      struct wlc_region *region = wl_resource_get_user_data(region_resource);
      pixman_region32_copy(&surface->pending.opaque, &region->region);
   } else {
      pixman_region32_clear(&surface->pending.opaque);
   }
}

static void
wl_cb_surface_set_input_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region_resource)
{
   (void)client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   if (region_resource) {
      struct wlc_region *region = wl_resource_get_user_data(region_resource);
      pixman_region32_copy(&surface->pending.input, &region->region);
   } else {
      pixman_region32_fini(&surface->pending.input);
      pixman_region32_init_rect(&surface->pending.input, INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
   }
}

static void
wl_cb_surface_commit(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);
   wlc_surface_commit_state(surface, &surface->pending);
}

static void
wl_cb_surface_set_buffer_transform(struct wl_client *client, struct wl_resource *resource, int32_t transform)
{
   (void)client, (void)resource, (void)transform;
   STUBL(resource);
}

static void
wl_cb_surface_set_buffer_scale(struct wl_client *client, struct wl_resource *resource, int32_t scale)
{
   (void)client, (void)resource, (void)scale;
   STUBL(resource);
}

static const struct wl_surface_interface wl_surface_implementation = {
   wl_cb_surface_destroy,
   wl_cb_surface_attach,
   wl_cb_surface_damage,
   wl_cb_surface_frame,
   wl_cb_surface_set_opaque_region,
   wl_cb_surface_set_input_region,
   wl_cb_surface_commit,
   wl_cb_surface_set_buffer_transform,
   wl_cb_surface_set_buffer_scale
};

static void
wl_cb_surface_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   if (surface) {
      surface->resource = NULL;
      wlc_surface_release(surface);
   }
}

void
wlc_surface_implement(struct wlc_surface *surface, struct wl_resource *resource)
{
   assert(surface);

   if (surface->resource == resource)
      return;

   if (surface->resource)
      wl_resource_destroy(surface->resource);

   surface->resource = resource;
   wl_resource_set_implementation(surface->resource, &wl_surface_implementation, surface, wl_cb_surface_destructor);
}

struct wlc_surface*
wlc_surface_ref(struct wlc_surface *surface)
{
   assert(surface);
   surface->ref_count += 1;
   return surface;
}

void
wlc_surface_release(struct wlc_surface *surface)
{
   assert(surface);

   if (--surface->ref_count > 0)
      return;

   if (surface->resource)
      wl_resource_destroy(surface->resource);

   if (surface->compositor)
      wl_list_remove(&surface->link);

   free(surface);
}

struct wlc_surface*
wlc_surface_new(struct wlc_compositor *compositor)
{
   struct wlc_surface *surface;
   if (!(surface = calloc(1, sizeof(struct wlc_surface))))
      return NULL;

   wl_list_insert(&compositor->surfaces, &surface->link);
   surface->compositor = compositor;
   surface->ref_count = 1;
   return surface;
}
