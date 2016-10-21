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
#include <chck/math/math.h>

static void
surface_attach(struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   assert(surface);

   struct wlc_output *output;
   if (!(output = convert_from_wlc_handle(surface->output, "output")))
      return;

   wlc_surface_attach_to_output(surface, output, buffer);

   struct wlc_view *view;
   if ((view = convert_from_wlc_handle(surface->view, "view"))) {
      if (buffer) {
         wlc_view_map(view);
         wlc_view_ack_surface_attach(view, surface);
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
   out->scale = chck_max32(pending->scale, 1);
   pending->offset = wlc_point_zero;

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

   if (pending->attached) {
      surface_attach(surface, convert_from_wlc_resource(pending->buffer, "buffer"));
      pending->attached = false;
   }

   state_set_buffer(out, convert_from_wlc_resource(pending->buffer, "buffer"));
   state_set_buffer(pending, NULL);
}

static void
init_state(struct wlc_surface_state *state)
{
   assert(state);
   pixman_region32_init_rect(&state->opaque, 0, 0, 0, 0);
   pixman_region32_init_rect(&state->damage, 0, 0, 0, 0);
   pixman_region32_init_rect(&state->input, INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
   state->scale = 1;
   state->subsurface_position = (struct wlc_point){0, 0};
}

static void
release_state(struct wlc_surface_state *state)
{
   if (!state)
      return;

   pixman_region32_fini(&state->opaque);
   pixman_region32_fini(&state->damage);
   pixman_region32_fini(&state->input);

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

   surface->pending.offset = (struct wlc_point){ x, y };
   surface->pending.attached = true;

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
commit_subsurface_state(struct wlc_surface *surface)
{
   if (!surface)
      return;

   commit_state(surface, &surface->pending, &surface->commit);
   wlc_output_schedule_repaint(convert_from_wlc_handle(surface->output, "output"));
   wlc_dlog(WLC_DBG_RENDER, "-> Commit request");

   wlc_resource *r;
   chck_iter_pool_for_each(&surface->subsurface_list, r) {
      struct wlc_surface *sub;
      if (!(sub = convert_from_wlc_resource(*r, "surface")))
         continue;

      sub->commit.subsurface_position = sub->pending.subsurface_position;
      if (sub->synchronized || sub->parent_synchronized)
         commit_subsurface_state(sub);
   }
}

static void
wl_cb_surface_commit(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   if (surface->parent_synchronized || surface->synchronized) {
      return;
   } else {
      commit_subsurface_state(surface);
   }
}

static void
wl_cb_surface_set_buffer_transform(struct wl_client *client, struct wl_resource *resource, int32_t transform)
{
   (void)client, (void)resource, (void)transform;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   if (transform < 0 || transform > WL_OUTPUT_TRANSFORM_FLIPPED_270) {
      wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_TRANSFORM, "buffer transform must be a valid transform (%d specified)", transform);
      return;
   }

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

   surface->pending.scale = scale;
}

bool
wlc_surface_get_opaque(struct wlc_surface *surface, const struct wlc_point *offset, struct wlc_geometry *out_opaque)
{
   *out_opaque = wlc_geometry_zero;

   if (!surface)
      return false;

   assert(offset && out_opaque);
   const bool opaque = ((surface->commit.opaque.extents.x1 + surface->commit.opaque.extents.y1 + surface->commit.opaque.extents.x2 + surface->commit.opaque.extents.y2) > 0);

   if (opaque) {
      struct wlc_geometry o = {
         .origin = {
            chck_min32(surface->commit.opaque.extents.x1, surface->size.w),
            chck_min32(surface->commit.opaque.extents.y1, surface->size.h),
         },
      };

      o.size.w = chck_clamp32(surface->commit.opaque.extents.x2, o.origin.x, surface->size.w),
      o.size.h = chck_clamp32(surface->commit.opaque.extents.y2, o.origin.y, surface->size.h),
      assert((int32_t)o.size.w >= o.origin.x && (int32_t)o.size.h >= o.origin.y);

      struct wlc_geometry v;
      v.origin.x = offset->x + o.origin.x * surface->coordinate_transform.w;
      v.origin.y = offset->y + o.origin.y * surface->coordinate_transform.h;
      v.size.w = (o.size.w - o.origin.x) * surface->coordinate_transform.w;
      v.size.h = (o.size.h - o.origin.y) * surface->coordinate_transform.h;
      *out_opaque = v;
   } else {
      struct wlc_geometry v = {
         .origin = *offset,
         .size = {
            .w = surface->size.w * surface->coordinate_transform.w,
            .h = surface->size.h * surface->coordinate_transform.h,
         }
      };
      *out_opaque = v;
   }

   return opaque;
}

void
wlc_surface_get_input(struct wlc_surface *surface, const struct wlc_point *offset, struct wlc_geometry *out_input)
{
   *out_input = wlc_geometry_zero;

   if (!surface)
      return;

   assert(offset && out_input);
   assert(surface->commit.input.extents.x2 >= surface->commit.input.extents.x1);
   assert(surface->commit.input.extents.y2 >= surface->commit.input.extents.y1);

   struct wlc_geometry v;
   v.origin.x = offset->x + surface->commit.input.extents.x1 * surface->coordinate_transform.w;
   v.origin.y = offset->y + surface->commit.input.extents.y1 * surface->coordinate_transform.h;
   v.size.w = (surface->commit.input.extents.x2 - surface->commit.input.extents.x1) * surface->coordinate_transform.w;
   v.size.h = (surface->commit.input.extents.y2 - surface->commit.input.extents.y1) * surface->coordinate_transform.h;
   *out_input = v;
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
   surface->view = surface->parent_view = convert_to_wlc_handle(view);
   wlc_view_set_surface(convert_from_wlc_handle(old, "view"), NULL);
   wlc_view_set_surface(view, surface);
}

bool
wlc_surface_attach_to_output(struct wlc_surface *surface, struct wlc_output *output, struct wlc_buffer *buffer)
{
   if (!output || !surface || !wlc_output_surface_attach(output, surface, buffer))
      return false;

   struct wlc_size size = wlc_size_zero;

   if (buffer)
      size = buffer->size;

   wlc_size_max(&size, &(struct wlc_size){1, 1}, &size);
   size.w /= surface->commit.scale;
   size.h /= surface->commit.scale;
   surface->size = size;

   struct wlc_view *view;
   if (surface->view && (view = convert_from_wlc_handle(surface->view, "view"))) {
      struct wlc_geometry g, area;
      wlc_view_get_bounds(view, &g, &area);
      surface->coordinate_transform.w = (float)(area.size.w) / size.w;
      surface->coordinate_transform.h = (float)(area.size.h) / size.h;
   } else {
      surface->coordinate_transform = (struct wlc_coordinate_scale) {1, 1};
   }

   struct wlc_surface *p;
   if ((p = convert_from_wlc_resource(surface->parent, "surface"))) {
      surface->coordinate_transform.w *= p->coordinate_transform.w;
      surface->coordinate_transform.h *= p->coordinate_transform.h;
   }

   surface->commit.attached = (buffer ? true : false);
   return true;
}

void
wlc_surface_set_parent(struct wlc_surface *surface, struct wlc_surface *parent)
{
   if (!surface)
      return;

   const wlc_resource newp = convert_to_wlc_resource(parent);
   if (surface->parent == newp)
      return;

   struct wlc_surface *p;
   if ((p = convert_from_wlc_resource(surface->parent, "surface"))) {
      wlc_resource *sub;
      const wlc_resource surface_id = convert_to_wlc_resource(surface);
      chck_iter_pool_for_each(&p->subsurface_list, sub) {
         if (*sub != surface_id)
            continue;

         chck_iter_pool_remove(&p->subsurface_list, _I - 1);
         break;
      }
   }

   const wlc_resource r = convert_to_wlc_resource(surface);
   if (parent && chck_iter_pool_push_front(&parent->subsurface_list, &r)) {
      wlc_surface_attach_to_output(surface, convert_from_wlc_handle(parent->output, "output"), wlc_surface_get_buffer(surface));
      surface->parent = newp;
      surface->parent_view = parent->parent_view;
   } else {
      surface->parent = 0;
   }
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
   wlc_surface_set_parent(surface, NULL);

   chck_iter_pool_for_each_call(&surface->subsurface_list, wlc_resource_release_ptr);
   chck_iter_pool_release(&surface->subsurface_list);

   wlc_surface_invalidate(surface);

   release_state(&surface->commit);
   release_state(&surface->pending);

   wlc_source_release(&surface->buffers);
   wlc_source_release(&surface->callbacks);
}

void
wlc_surface_commit(struct wlc_surface *surface)
{
   assert(surface);
   commit_state(surface, &surface->pending, &surface->commit);
}

bool
wlc_surface(struct wlc_surface *surface)
{
   assert(surface);

   if (!wlc_source(&surface->buffers, "buffer", wlc_buffer, wlc_buffer_release, 4, sizeof(struct wlc_buffer)) ||
       !wlc_source(&surface->callbacks, "callback", NULL, NULL, 4, sizeof(struct wlc_resource)))
      goto fail;

   if (!chck_iter_pool(&surface->commit.frame_cbs, 4, 0, sizeof(wlc_resource)) ||
       !chck_iter_pool(&surface->pending.frame_cbs, 4, 0, sizeof(wlc_resource)) ||
       !chck_iter_pool(&surface->subsurface_list, 4, 0, sizeof(wlc_resource)))
      goto fail;

   init_state(&surface->pending);
   init_state(&surface->commit);
   surface->coordinate_transform = (struct wlc_coordinate_scale){1, 1};
   surface->parent_synchronized = false;
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
