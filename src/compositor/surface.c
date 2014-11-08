#include "internal.h"
#include "surface.h"
#include "compositor.h"
#include "output.h"
#include "view.h"
#include "region.h"
#include "buffer.h"
#include "callback.h"
#include "macros.h"

#include "platform/render/render.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <wayland-server.h>

static void
surface_attach(struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   struct wlc_space *space = (buffer && surface->view ? wlc_view_get_mapped_space(surface->view) : NULL);

   // We automatically attach view surfaces.
   // Everything else like cursors etc, needs to be mapped once explicitly.
   // We know what resources we use.
   struct wlc_output *output = (space ? space->output : surface->output);

   // Old surface size for xdg-surface commit
   struct wlc_size old_size = surface->size;

   if (output)
      wlc_surface_attach_to_output(surface, output, buffer);

   // Change view space only if surface has no space already (unmapped)
   // Or the buffer is NULL (unmaps the view)
   if (surface->view) {
      if (!surface->view->space || !buffer)
         wlc_view_set_space(surface->view, space);

      // The view may not be created if the API user does not want to
      if (surface->view)
         wlc_view_ack_surface_attach(surface->view, &old_size);
   }
}

static void
state_set_buffer(struct wlc_surface_state *state, struct wlc_buffer *buffer)
{
   if (state->buffer == buffer)
      return;

   if (state->buffer)
      wlc_buffer_free(state->buffer);

   state->buffer = wlc_buffer_use(buffer);
}

static void
commit_state(struct wlc_surface *surface, struct wlc_surface_state *pending, struct wlc_surface_state *out)
{
   if (pending->attached) {
      surface_attach(surface, pending->buffer);
      pending->attached = false;
   }

   state_set_buffer(out, pending->buffer);
   state_set_buffer(pending, NULL);

   pending->offset = wlc_origin_zero;

   wl_list_insert_list(&out->frame_cb_list, &pending->frame_cb_list);
   wl_list_init(&pending->frame_cb_list);

   pixman_region32_union(&out->damage, &out->damage, &pending->damage);
   pixman_region32_intersect_rect(&out->damage, &out->damage, 0, 0, surface->size.w, surface->size.h);
   pixman_region32_clear(&surface->pending.damage);

   pixman_region32_t opaque;
   pixman_region32_init(&opaque);
   pixman_region32_intersect_rect(&opaque, &pending->opaque, 0, 0, surface->size.w, surface->size.h);

   if (!pixman_region32_equal(&opaque, &out->opaque))
      pixman_region32_copy(&out->opaque, &opaque);

   surface->opaque = (opaque.extents.x1 == 0 && opaque.extents.y1 == 0 &&
                      opaque.extents.x2 == (int32_t)surface->size.w && opaque.extents.y2 == (int32_t)surface->size.h);

   pixman_region32_fini(&opaque);

   pixman_region32_intersect_rect(&out->input, &pending->input, 0, 0, surface->size.w, surface->size.h);
}

static void
release_state(struct wlc_surface_state *state)
{
   if (state->buffer)
      wlc_buffer_free(state->buffer);

   struct wlc_callback *cb, *cbn;
   wl_list_for_each_safe(cb, cbn, &state->frame_cb_list, link)
      wlc_callback_free(cb);
}

static void
wl_cb_surface_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   wl_resource_destroy(resource);
}

static void
wl_cb_surface_attach(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *buffer_resource, int32_t x, int32_t y)
{
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   // We can't set or get buffer_resource user data.
   // It seems to be owned by somebody else?
   // What use is user data which we can't use...
   //
   // According to #wayland, user data isn't actually user data, but internal data of the resource.
   // We only own the user data if the resource was created by us.
   //
   // See what wlc_buffer_resource_get_container does, we implement destroy listener which we need anyways,
   // and the container_of macro to get owner.

   struct wlc_buffer *buffer = NULL;
   if (buffer_resource && !(buffer = wlc_buffer_resource_get_container(buffer_resource)) && !(buffer = wlc_buffer_new(buffer_resource))) {
      wl_client_post_no_memory(wl_client);
      return;
   }

   state_set_buffer(&surface->pending, buffer);

   surface->pending.offset = (struct wlc_origin){ x, y };
   surface->pending.attached = true;

   wlc_dlog(WLC_DBG_RENDER, "-> Attach request");
}

static void
wl_cb_surface_damage(struct wl_client *wl_client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)wl_client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);
   pixman_region32_union_rect(&surface->pending.damage, &surface->pending.damage, x, y, width, height);
   wlc_dlog(WLC_DBG_RENDER, "-> Damage request");
}

static void
wl_cb_surface_frame(struct wl_client *wl_client, struct wl_resource *resource, uint32_t callback_id)
{
   struct wl_resource *callback_resource;
   if (!(callback_resource = wl_resource_create(wl_client, &wl_callback_interface, wl_resource_get_version(resource), callback_id)))
      goto fail;

   struct wlc_callback *callback;
   if (!(callback = wlc_callback_new(callback_resource)))
      goto fail;

   wlc_callback_implement(callback);

   struct wlc_surface *surface = wl_resource_get_user_data(resource);
   wl_list_insert(surface->pending.frame_cb_list.prev, &callback->link);
   wlc_dlog(WLC_DBG_RENDER, "-> Frame request");
   return;

fail:
   if (callback_resource)
      wl_resource_destroy(callback_resource);
   wl_resource_post_no_memory(resource);
}

static void
wl_cb_surface_set_opaque_region(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *region_resource)
{
   (void)wl_client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   if (region_resource) {
      struct wlc_region *region = wl_resource_get_user_data(region_resource);
      pixman_region32_copy(&surface->pending.opaque, &region->region);
   } else {
      pixman_region32_clear(&surface->pending.opaque);
   }
}

static void
wl_cb_surface_set_input_region(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *region_resource)
{
   (void)wl_client;
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
wl_cb_surface_commit(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   commit_state(surface, &surface->pending, &surface->commit);

   if (surface->output)
      wlc_output_schedule_repaint(surface->output);

   wlc_dlog(WLC_DBG_RENDER, "-> Commit request");
}

static void
wl_cb_surface_set_buffer_transform(struct wl_client *wl_client, struct wl_resource *resource, int32_t transform)
{
   (void)wl_client, (void)resource, (void)transform;
   STUBL(resource);
}

static void
wl_cb_surface_set_buffer_scale(struct wl_client *wl_client, struct wl_resource *resource, int32_t scale)
{
   (void)wl_client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   if (scale < 1) {
      wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE, "scale must be >= 1 (scale: %d)", scale);
      return;
   }

   surface->pending.scale = 1;
}

static const struct wl_surface_interface wl_surface_implementation = {
   .destroy = wl_cb_surface_destroy,
   .attach = wl_cb_surface_attach,
   .damage = wl_cb_surface_damage,
   .frame = wl_cb_surface_frame,
   .set_opaque_region = wl_cb_surface_set_opaque_region,
   .set_input_region = wl_cb_surface_set_input_region,
   .commit = wl_cb_surface_commit,
   .set_buffer_transform = wl_cb_surface_set_buffer_transform,
   .set_buffer_scale = wl_cb_surface_set_buffer_scale
};

static void
wl_cb_surface_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   if (surface) {
      surface->resource = NULL;
      wlc_surface_free(surface);
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

void
wlc_surface_attach_to_output(struct wlc_surface *surface, struct wlc_output *output, struct wlc_buffer *buffer)
{
   assert(output);

   if (!wlc_output_surface_attach(output, surface, buffer))
      return;

   struct wlc_size size = wlc_size_zero;

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
      size = buffer->size;
   }

   surface->size = size;
   surface->commit.attached = true;
}

void
wlc_surface_invalidate(struct wlc_surface *surface)
{
   if (!surface->output)
      return;

   wlc_output_surface_destroy(surface->output, surface);
}

void
wlc_surface_free(struct wlc_surface *surface)
{
   assert(surface);

   if (surface->resource) {
      wl_resource_destroy(surface->resource);
      return;
   }

   if (surface->view)
      wlc_view_free(surface->view);

   wlc_surface_invalidate(surface);

   release_state(&surface->commit);
   release_state(&surface->pending);

   free(surface);
}

struct wlc_surface*
wlc_surface_new(void)
{
   struct wlc_surface *surface;
   if (!(surface = calloc(1, sizeof(struct wlc_surface))))
      return NULL;

   wl_list_init(&surface->commit.frame_cb_list);
   wl_list_init(&surface->pending.frame_cb_list);
   return surface;
}
