#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <wayland-server.h>
#include "internal.h"
#include "surface.h"
#include "region.h"
#include "buffer.h"
#include "macros.h"
#include "compositor/output.h"
#include "compositor/view.h"

static void
surface_attach(struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   assert(surface);

   struct wlc_output *output;
   if (!(output = convert_from_wlc_handle(surface->output, "output")))
      return;

   // Old surface size for xdg-surface commit
   struct wlc_size old_size = surface->size;

   if (output)
      wlc_surface_attach_to_output(surface, output, buffer);

   struct wlc_view *view;
   if ((view = convert_from_wlc_handle(surface->view, "view"))) {
      if (buffer) {
         wlc_view_map(view);
         wlc_view_ack_surface_attach(view, surface, &old_size);
      } else {
         wlc_view_unmap(view);
      }
   }
}

static void
state_set_buffer(struct wlc_surface_state *state, struct wlc_buffer *buffer)
{
   if (state->buffer == convert_to_wlc_resource(buffer))
      return;

   wlc_buffer_dispose(convert_from_wlc_resource(state->buffer, "buffer"));
   state->buffer = wlc_buffer_use(buffer);
}

static void
commit_state(struct wlc_surface *surface, struct wlc_surface_state *pending, struct wlc_surface_state *out)
{
   if (pending->attached) {
      surface_attach(surface, convert_from_wlc_resource(pending->buffer, "buffer"));
      pending->attached = false;
   }

   state_set_buffer(out, convert_from_wlc_resource(pending->buffer, "buffer"));
   state_set_buffer(pending, NULL);

   pending->offset = wlc_origin_zero;

   wlc_resource *r;
   chck_iter_pool_for_each(&pending->frame_cbs, r)
      chck_iter_pool_push_back(&out->frame_cbs, r);
   chck_iter_pool_flush(&pending->frame_cbs);

   pixman_region32_union(&out->damage, &out->damage, &pending->damage);
   pixman_region32_intersect_rect(&out->damage, &out->damage, 0, 0, surface->size.w, surface->size.h);
   pixman_region32_clear(&surface->pending.damage);

   pixman_region32_t opaque;
   pixman_region32_init(&opaque);
   pixman_region32_intersect_rect(&opaque, &pending->opaque, 0, 0, surface->size.w, surface->size.h);

   if (!pixman_region32_equal(&opaque, &out->opaque))
      pixman_region32_copy(&out->opaque, &opaque);

   pixman_region32_fini(&opaque);

   pixman_region32_intersect_rect(&out->input, &pending->input, 0, 0, surface->size.w, surface->size.h);
}

static void
release_state(struct wlc_surface_state *state)
{
   if (!state)
      return;

   state_set_buffer(state, 0);
   chck_iter_pool_for_each_call(&state->frame_cbs, wlc_resource_release_ptr);
   chck_iter_pool_release(&state->frame_cbs);
}

static void
wl_cb_surface_attach(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer_resource, int32_t x, int32_t y)
{
   (void)client;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   wlc_resource buffer = 0;
   if (buffer_resource && !(buffer = wlc_resource_from_wl_resource(buffer_resource)) && !(buffer = wlc_resource_create_from(&surface->buffers, buffer_resource))) {
      wl_client_post_no_memory(client);
      return;
   }

   struct wlc_buffer *b;
   if ((b = convert_from_wlc_resource(buffer, "buffer"))) {
      b->surface = convert_to_wlc_resource(surface);
      state_set_buffer(&surface->pending, b);
   }

   surface->pending.offset = (struct wlc_origin){ x, y };
   surface->pending.attached = (buffer ? true : false);

   wlc_dlog(WLC_DBG_RENDER, "-> Attach request");
}

static void
wl_cb_surface_damage(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)client;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   pixman_region32_union_rect(&surface->pending.damage, &surface->pending.damage, x, y, width, height);
   wlc_dlog(WLC_DBG_RENDER, "-> Damage request");
}

static void
wl_cb_surface_frame(struct wl_client *client, struct wl_resource *resource, uint32_t callback_id)
{
   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&surface->callbacks, client, &wl_callback_interface, wl_resource_get_version(resource), 3, callback_id)))
      return;

   wlc_resource_implement(r, NULL, NULL);
   chck_iter_pool_push_back(&surface->pending.frame_cbs, &r);
   wlc_dlog(WLC_DBG_RENDER, "-> Frame request");
}

static void
wl_cb_surface_set_opaque_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region_resource)
{
   (void)client;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   struct wlc_region *region;
   if (region_resource && (region = convert_from_wl_resource(region_resource, "region"))) {
      pixman_region32_copy(&surface->pending.opaque, &region->region);
   } else {
      pixman_region32_clear(&surface->pending.opaque);
   }
}

static void
wl_cb_surface_set_input_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region_resource)
{
   (void)client;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   struct wlc_region *region;
   if (region_resource && (region = convert_from_wl_resource(region_resource, "region"))) {
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

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   commit_state(surface, &surface->pending, &surface->commit);
   wlc_output_schedule_repaint(convert_from_wlc_handle(surface->output, "output"));
   wlc_dlog(WLC_DBG_RENDER, "-> Commit request");
}

static void
wl_cb_surface_set_buffer_transform(struct wl_client *client, struct wl_resource *resource, int32_t transform)
{
   (void)client, (void)resource, (void)transform;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   surface->pending.transform = transform;
}

static void
wl_cb_surface_set_buffer_scale(struct wl_client *client, struct wl_resource *resource, int32_t scale)
{
   (void)client;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   if (scale < 0) {
      wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE, "scale must be >= 0 (scale: %d)", scale);
      return;
   }

   surface->pending.scale = 1;
}

struct wlc_buffer*
wlc_surface_get_buffer(struct wlc_surface *surface)
{
   if (!surface)
      return NULL;

   return convert_from_wlc_resource((surface->commit.buffer ? surface->commit.buffer : surface->pending.buffer), "buffer");
}

void
wlc_surface_attach_to_view(struct wlc_surface *surface, struct wlc_view *view)
{
   if (!surface || surface->view == convert_to_wlc_handle(view))
      return;

   wlc_handle old = surface->view;
   surface->view = convert_to_wlc_handle(view);
   wlc_view_set_surface(convert_from_wlc_handle(old, "view"), NULL);
   wlc_view_set_surface(view, surface);
}

bool
wlc_surface_attach_to_output(struct wlc_surface *surface, struct wlc_output *output, struct wlc_buffer *buffer)
{
   assert(output);

   if (!surface || !wlc_output_surface_attach(output, surface, buffer))
      return false;

   struct wlc_size size = wlc_size_zero;

   if (buffer)
      size = buffer->size;

   surface->size = size;
   surface->commit.attached = (buffer ? true : false);
   return true;
}

void
wlc_surface_set_parent(struct wlc_surface *surface, struct wlc_surface *parent)
{
   if (!surface)
      return;

   surface->parent = convert_to_wlc_resource(parent);
}

void
wlc_surface_invalidate(struct wlc_surface *surface)
{
   if (!surface)
      return;

   wlc_output_surface_destroy(convert_from_wlc_handle(surface->output, "output"), surface);
}

void
wlc_surface_release(struct wlc_surface *surface)
{
   if (!surface)
      return;

   struct wlc_surface_event ev = { .surface = surface, .type = WLC_SURFACE_EVENT_DESTROYED };
   wl_signal_emit(&wlc_system_signals()->surface, &ev);

   wlc_handle_release(surface->view);
   wlc_surface_invalidate(surface);

   release_state(&surface->commit);
   release_state(&surface->pending);

   wlc_source_release(&surface->buffers);
   wlc_source_release(&surface->callbacks);
}

bool
wlc_surface(struct wlc_surface *surface)
{
   assert(surface);

   if (!wlc_source(&surface->buffers, "buffer", wlc_buffer, wlc_buffer_release, 4, sizeof(struct wlc_buffer)) ||
       !wlc_source(&surface->callbacks, "callback", NULL, NULL, 4, sizeof(struct wlc_resource)))
      goto fail;

   if (!chck_iter_pool(&surface->commit.frame_cbs, 4, 0, sizeof(wlc_resource)) ||
       !chck_iter_pool(&surface->pending.frame_cbs, 4, 0, sizeof(wlc_resource)))
      goto fail;

   return true;

fail:
   wlc_surface_release(surface);
   return false;
}

WLC_CONST const struct wl_surface_interface*
wlc_surface_implementation(void)
{
   static const struct wl_surface_interface wl_surface_implementation = {
      .destroy = wlc_cb_resource_destructor,
      .attach = wl_cb_surface_attach,
      .damage = wl_cb_surface_damage,
      .frame = wl_cb_surface_frame,
      .set_opaque_region = wl_cb_surface_set_opaque_region,
      .set_input_region = wl_cb_surface_set_input_region,
      .commit = wl_cb_surface_commit,
      .set_buffer_transform = wl_cb_surface_set_buffer_transform,
      .set_buffer_scale = wl_cb_surface_set_buffer_scale
   };

   return &wl_surface_implementation;
}
