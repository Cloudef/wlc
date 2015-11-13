#include "output.h"
#include <stdlib.h>
#include <limits.h>
#include <wayland-server.h>
#include <chck/string/string.h>
#include <chck/math/math.h>
#include <chck/overflow/overflow.h>
#include "internal.h"
#include "visibility.h"
#include "macros.h"
#include "output.h"
#include "view.h"
#include "resources/types/surface.h"

// FIXME: this is a hack
static EGLNativeDisplayType INVALID_DISPLAY = (EGLNativeDisplayType)~0;

WLC_PURE static const char*
name_for_connector(enum wlc_connector_type connector)
{
   switch (connector) {
      case WLC_CONNECTOR_WLC: return "WLC";
      case WLC_CONNECTOR_UNKNOWN: return "Unknown";
      case WLC_CONNECTOR_VGA: return "VGA";
      case WLC_CONNECTOR_DVII: return "DVI-I";
      case WLC_CONNECTOR_DVID: return "DVI-D";
      case WLC_CONNECTOR_DVIA: return "DVI-A";
      case WLC_CONNECTOR_COMPOSITE: return "Composite";
      case WLC_CONNECTOR_SVIDEO: return "SVIDEO";
      case WLC_CONNECTOR_LVDS: return "LVDS";
      case WLC_CONNECTOR_COMPONENT: return "Component";
      case WLC_CONNECTOR_DIN: return "DIN";
      case WLC_CONNECTOR_DP: return "DP";
      case WLC_CONNECTOR_HDMIA: return "HDMI-A";
      case WLC_CONNECTOR_HDMIB: return "HDMI-B";
      case WLC_CONNECTOR_TV: return "TV";
      case WLC_CONNECTOR_eDP: return "eDP";
      case WLC_CONNECTOR_VIRTUAL: return "Virtual";
      case WLC_CONNECTOR_DSI: return "DSI";
   }

   assert(0 && "something is missing from the list above");
   return "UNNAMED";
}

bool
wlc_output_information_add_mode(struct wlc_output_information *info, struct wlc_output_mode *mode)
{
   assert(info && mode);
   return chck_iter_pool_push_back(&info->modes, mode);
}

bool
wlc_output_information(struct wlc_output_information *info)
{
   assert(info);
   memset(info, 0, sizeof(struct wlc_output_information));
   return chck_iter_pool(&info->modes, 1, 0, sizeof(struct wlc_output_mode));
}

void
wlc_output_information_release(struct wlc_output_information *info)
{
   if (!info)
      return;

   chck_iter_pool_release(&info->modes);
   chck_string_release(&info->name);
   chck_string_release(&info->make);
   chck_string_release(&info->model);
}

static void
wl_output_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wlc_output *output = data;
   assert(output);

   wlc_resource r;
   if (!(r = wlc_resource_create(&output->resources, client, &wl_output_interface, version, 2, id)))
      return;

   wlc_resource_implement(r, NULL, (void*)convert_to_wlc_handle(output));

   struct wl_resource *resource;
   if (!(resource = wl_resource_from_wlc_resource(r, "output")))
      return;

   // FIXME: update on wlc_output_set_information
   wl_output_send_geometry(resource, output->information.x, output->information.y,
                           output->information.physical_width, output->information.physical_height, output->information.subpixel,
                           (output->information.make.data ? output->information.make.data : "unknown"),
                           (output->information.model.data ? output->information.model.data : "model"),
                           output->information.transform);

   assert(output->information.scale > 0);
   if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
      wl_output_send_scale(resource, output->information.scale);

   struct wlc_output_mode *mode;
   chck_iter_pool_for_each(&output->information.modes, mode)
      wl_output_send_mode(resource, mode->flags, mode->width, mode->height, mode->refresh);

   if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
      wl_output_send_done(resource);
}

static bool
view_visible(struct wlc_view *view, struct wlc_surface *surface, uint32_t mask)
{
   if (!view || !surface)
      return false;

   return (surface->commit.attached && (view->mask & mask));
}

static bool
blit(bool *g, const struct wlc_size *r, const struct wlc_point *a, const struct wlc_point *b, bool should_blit)
{
   assert(g && r && a && b);

   bool visible = false;
   const size_t gsz = r->w * r->h;
   assert(b->x - a->x >= 0);
   assert(a->x >= 0 && b->x <= (int32_t)r->w && a->y >= 0 && b->y <= (int32_t)r->h);
   for (int32_t y = a->y; y < b->y; ++y) {
      const size_t off = a->x + y * r->w, memb = (b->x - a->x);
      assert(off + memb <= gsz);

      if (!visible && memchr(g + off, false, memb)) {
         visible = true;

         if (!should_blit)
            return true;
      }

      if (should_blit)
         memset(g + off, true, memb);
   }

   return visible;
}

static bool
get_visible_views(struct wlc_output *output, struct chck_iter_pool *visible)
{
   assert(output && output->blit);

   const size_t gsz = output->resolution.w * output->resolution.h;
   memset(output->blit, false, gsz);

   wlc_handle *h;
   chck_iter_pool_for_each_reverse(&output->views, h) {
      struct wlc_view *v;
      struct wlc_surface *s;
      if (!(v = convert_from_wlc_handle(*h, "view")) ||
          !(s = convert_from_wlc_resource(v->surface, "surface")))
         continue;

      if (!view_visible(v, s, output->active.mask))
         continue;

      struct wlc_geometry o;
      struct wlc_point a, b;
      const bool should_blit = wlc_view_get_opaque(v, &o);
      a.x = chck_clamp32(o.origin.x, 0, output->resolution.w);
      a.y = chck_clamp32(o.origin.y, 0, output->resolution.h);
      b.x = chck_clamp32((o.origin.x + o.size.w), 1, output->resolution.w);
      b.y = chck_clamp32((o.origin.y + o.size.h), 1, output->resolution.h);

      if (!blit(output->blit, &output->resolution, &a, &b, should_blit)) {
         wlc_dlog(WLC_DBG_RENDER_LOOP, "%" PRIuWLC " is not visible", *h);
         continue;
      }

      chck_iter_pool_push_front(visible, &v);
   }

   return memchr(output->blit, false, gsz);
}

static void
finish_frame_tasks(struct wlc_output *output)
{
   assert(output);

   if (output->task.bsurface.display) {
      wlc_output_set_backend_surface(output, (output->task.bsurface.display == INVALID_DISPLAY ? NULL : &output->task.bsurface));
      memset(&output->task.bsurface, 0, sizeof(output->task.bsurface));
   }

   if (output->task.sleep) {
      wlc_output_set_sleep_ptr(output, true);
      output->task.sleep = false;
   }

   if (output->task.terminate) {
      wlc_output_terminate(output);
      output->task.terminate = false;
   }
}

static void
render_view(struct wlc_output *output, struct wlc_view *view, struct chck_iter_pool *callbacks)
{
   assert(output && callbacks);

   if (!view)
      return;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wlc_resource(view->surface, "surface")))
      return;

   if (!view_visible(view, surface, output->active.mask))
      return;

   wlc_view_commit_state(view, &view->pending, &view->commit);
   wlc_render_view_paint(&output->render, &output->context, view);

   wlc_resource *r;
   chck_iter_pool_for_each(&surface->commit.frame_cbs, r)
      chck_iter_pool_push_back(callbacks, r);
   chck_iter_pool_flush(&surface->commit.frame_cbs);
}

static bool
should_render(struct wlc_output *output)
{
   assert(output);
   return (wlc_get_active() && !output->state.pending && output->bsurface.display && output->active.mode != UINT_MAX);
}

static bool
repaint(struct wlc_output *output)
{
   if (!output)
      return false;

   if (!should_render(output)) {
      wlc_dlog(WLC_DBG_RENDER_LOOP, "-> Skipped repaint");
      output->state.activity = output->state.scheduled = false;
      finish_frame_tasks(output);
      return false;
   }

   wlc_render_time(&output->render, &output->context, output->state.frame_time);
   wlc_render_resolution(&output->render, &output->context, &output->mode, &output->resolution);

   if (output->state.sleeping) {
      // fake sleep
      wlc_render_clear(&output->render, &output->context);
      output->state.pending = true;
      wlc_context_swap(&output->context, &output->bsurface);
      wlc_dlog(WLC_DBG_RENDER_LOOP, "-> Repaint");
      return true;
   }

   const bool bg_visible = get_visible_views(output, &output->visible);

   if (!output->state.background_visible && bg_visible) {
      wlc_dlog(WLC_DBG_RENDER_LOOP, "-> Background visible");
      output->state.background_visible = true;
   } else if (output->state.background_visible && !bg_visible) {
      wlc_dlog(WLC_DBG_RENDER_LOOP, "-> Background not visible");
      output->state.background_visible = false;
   }

   if (output->options.enable_bg && output->state.background_visible) {
      wlc_render_background(&output->render, &output->context);
   } else if (!output->options.enable_bg) {
      wlc_render_clear(&output->render, &output->context);
   }


   {
      struct wlc_view **v;
      chck_iter_pool_for_each(&output->visible, v)
         render_view(output, *v, &output->callbacks);
      chck_iter_pool_flush(&output->visible);
   }

   struct wlc_render_event ev = { .output = output, .type = WLC_RENDER_EVENT_POINTER };
   wl_signal_emit(&wlc_system_signals()->render, &ev);

   {
      size_t sz;
      void *rgba;
      struct wlc_geometry g = { { 0, 0 }, output->resolution };
      if (output->task.pixels.cb && !chck_mul_ofsz(g.size.w, g.size.h, &sz) && (rgba = chck_calloc_of(4, sz))) {
         wlc_render_read_pixels(&output->render, &output->context, &g, rgba);
         if (!output->task.pixels.cb(&g.size, rgba, output->task.pixels.arg))
            free(rgba);
         memset(&output->task.pixels, 0, sizeof(output->task.pixels));
      }
   }

   output->state.pending = true;
   wlc_context_swap(&output->context, &output->bsurface);

   {
      wlc_resource *r;
      chck_iter_pool_for_each(&output->callbacks, r) {
         struct wl_resource *resource;
         if ((resource = wl_resource_from_wlc_resource(*r, "callback")))
            wl_callback_send_done(resource, output->state.frame_time);
         wlc_resource_release_ptr(r);
      }
      chck_iter_pool_flush(&output->callbacks);
   }

   wlc_dlog(WLC_DBG_RENDER_LOOP, "-> Repaint");
   return true;
}

static int
cb_idle_timer(void *data)
{
   assert(data);
   repaint(convert_from_wlc_handle((wlc_handle)data, "output"));
   return 1;
}

static void
cancel_repaint(struct wlc_output *output)
{
   wl_event_source_timer_update(output->timer.idle, 0);
   output->state.scheduled = output->state.activity = false;
}

void
wlc_output_finish_frame(struct wlc_output *output, const struct timespec *ts)
{
   assert(ts);

   if (!output)
      return;

   output->state.pending = false;

   // XXX: uint32_t holds mostly for 50 days before overflowing
   //      is this tied to wayland somewhere, or should we increase precision?
   const uint32_t last = output->state.frame_time;
   output->state.frame_time = ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
   const uint32_t ms = output->state.frame_time - last;

   // TODO: handle presentation feedback here

   if (((output->options.enable_bg && output->state.background_visible) || output->state.activity) && !output->task.terminate) {
      output->state.ims = chck_clampf(output->state.ims * (output->state.activity ? 0.9 : 1.1), 1, 41);
      wlc_dlog(WLC_DBG_RENDER_LOOP, "-> Interpolated idle time %f (%u : %d)", output->state.ims, ms, output->state.activity);
      wl_event_source_timer_update(output->timer.idle, output->state.ims);
      output->state.scheduled = true;
      output->state.activity = false;
   } else {
      output->state.scheduled = false;
   }

   wlc_dlog(WLC_DBG_RENDER_LOOP, "-> Finished frame");
   finish_frame_tasks(output);
}

void
wlc_output_surface_destroy(struct wlc_output *output, struct wlc_surface *surface)
{
   if (!output)
      return;

   assert(surface && surface->output == convert_to_wlc_handle(output));

   wlc_render_surface_destroy(&output->render, &output->context, surface);
   surface->output = 0;

   wlc_output_schedule_repaint(output);

   wlc_resource *r;
   chck_iter_pool_for_each(&output->surfaces, r) {
      if (*r != convert_to_wlc_resource(surface))
         continue;

      chck_iter_pool_remove(&output->surfaces, _I - 1);
      break;
   }

   wlc_dlog(WLC_DBG_RENDER, "-> Deattached surface (%" PRIuWLC ") from output (%" PRIuWLC ")", convert_to_wlc_resource(surface), convert_to_wlc_handle(output));
}

bool
wlc_output_surface_attach(struct wlc_output *output, struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   assert(surface);

   if (!output)
      return false;

   bool new_surface = false;
   if (surface->output != convert_to_wlc_handle(output)) {
      wlc_surface_invalidate(surface);
      surface->output = convert_to_wlc_handle(output);
      new_surface = true;
   }

   if (!wlc_render_surface_attach(&output->render, &output->context, surface, buffer)) {
      surface->output = 0;
      return false;
   }

   if (new_surface) {
      wlc_resource r = convert_to_wlc_resource(surface);
      if (!chck_iter_pool_push_back(&output->surfaces, &r)) {
         wlc_surface_invalidate(surface);
         return false;
      }

      wlc_dlog(WLC_DBG_RENDER, "-> Attached surface (%" PRIuWLC ") to output (%" PRIuWLC ")", r, convert_to_wlc_handle(output));
   }

   wlc_output_schedule_repaint(output);
   return true;
}

static bool
attach_view(struct wlc_output *output, struct wlc_view *view)
{
   struct wlc_surface *surface;
   if (!output || !view || !(surface = convert_from_wlc_resource(view->surface, "surface")))
      return false;

   struct wlc_buffer *buffer;
   if (!(buffer = wlc_surface_get_buffer(surface)))
      return false;

   return wlc_surface_attach_to_output(surface, output, buffer);
}

void
wlc_output_schedule_repaint(struct wlc_output *output)
{
   if (!output)
      return;

   if (!output->state.activity)
      wlc_dlog(WLC_DBG_RENDER_LOOP, "-> Activity marked");

   output->state.activity = true;

   if (output->state.scheduled)
      return;

   output->state.scheduled = true;
   wl_event_source_timer_update(output->timer.idle, 1);
   wlc_dlog(WLC_DBG_RENDER_LOOP, "-> Repaint scheduled");
}

bool
wlc_output_set_backend_surface(struct wlc_output *output, struct wlc_backend_surface *bsurface)
{
   assert(output);

   if (output->bsurface.display == (bsurface ? bsurface->display : 0))
      return true;

   if (output->state.pending) {
      wlc_log(WLC_LOG_INFO, "Pending bsurface set for output (%" PRIuWLC ")", convert_to_wlc_handle(output));
      if (bsurface) {
         memcpy(&output->task.bsurface, bsurface, sizeof(output->task.bsurface));
      } else {
         output->task.bsurface.display = INVALID_DISPLAY;
      }
      return true;
   }

   {
      wlc_resource *r;
      chck_iter_pool_for_each(&output->surfaces, r) {
         struct wlc_surface *s;
         if ((s = convert_from_wlc_resource(*r, "surface")))
            wlc_render_surface_destroy(&output->render, &output->context, s);
      }
   }

   wlc_render_release(&output->render, &output->context);
   wlc_context_release(&output->context);
   wlc_backend_surface_release(&output->bsurface);

   if (bsurface) {
      memcpy(&output->bsurface, bsurface, sizeof(output->bsurface));
      memset(bsurface, 0, sizeof(output->bsurface));
   }

   if (bsurface) {
      if (!wlc_context(&output->context, &output->bsurface))
         goto fail;

      wlc_context_bind_to_wl_display(&output->context, wlc_display());

      if (!wlc_render(&output->render, &output->context))
         goto fail;

      {
         wlc_resource *r;
         chck_iter_pool_for_each(&output->surfaces, r) {
            struct wlc_surface *s;
            if (!(s = convert_from_wlc_resource(*r, "surface")))
               continue;

            wlc_surface_attach_to_output(s, output, wlc_surface_get_buffer(s));
         }
      }

      wlc_log(WLC_LOG_INFO, "Set new bsurface to output (%" PRIuWLC ")", convert_to_wlc_handle(output));
   } else {
      wlc_log(WLC_LOG_INFO, "Removed bsurface from output (%" PRIuWLC ")", convert_to_wlc_handle(output));
      cancel_repaint(output);
   }

   struct wlc_output_event ev = { .surface = { .output = output }, .type = WLC_OUTPUT_EVENT_SURFACE };
   wl_signal_emit(&wlc_system_signals()->output, &ev);
   return true;

fail:
   wlc_output_set_backend_surface(output, NULL);
   return false;
}

void
wlc_output_set_information(struct wlc_output *output, struct wlc_output_information *info)
{
   assert(output);
   wlc_output_information_release(&output->information);

   if (info) {
      assert(!info->name.data && "Do not set name, this function will do that automatically");
      memcpy(&output->information, info, sizeof(output->information));
      memset(info, 0, sizeof(output->information));
   }

   output->active.mode = UINT_MAX;

   if (!info)
      return;

   {
      struct wlc_output_mode *mode;
      chck_iter_pool_for_each(&output->information.modes, mode) {
         if (mode->flags & WL_OUTPUT_MODE_CURRENT || (output->active.mode == UINT_MAX && (mode->flags & WL_OUTPUT_MODE_PREFERRED)))
            output->active.mode = _I - 1;
      }
   }

   assert(output->active.mode != UINT_MAX && "output should have at least one current mode!");

   const char *name = name_for_connector(output->information.connector);
   chck_string_set_format(&output->information.name, "%s-%u", name, output->information.connector_id);

   {
      struct wlc_output_mode *mode;
      except(mode = chck_iter_pool_get(&output->information.modes, output->active.mode));
      wlc_log(WLC_LOG_INFO, "%s Chose mode (%u) %dx%d", output->information.name.data, output->active.mode, mode->width, mode->height);
      output->mode = (struct wlc_size){ mode->width, mode->height };
      wlc_output_set_resolution_ptr(output, &output->mode);
   }
}

static void
remove_from_pool(struct chck_iter_pool *pool, wlc_handle handle)
{
   wlc_handle *h;
   chck_iter_pool_for_each(pool, h) {
      if (*h != handle)
         continue;

      chck_iter_pool_remove(pool, _I - 1);
      break;
   }
}

static bool
exists_in_pool(struct chck_iter_pool *pool, wlc_handle handle)
{
   wlc_handle *h;
   chck_iter_pool_for_each(pool, h) {
      if (*h == handle)
         return true;
   }
   return false;
}

void
wlc_output_unlink_view(struct wlc_output *output, struct wlc_view *view)
{
   if (!output || wlc_view_get_output_ptr(view) != output)
      return;

   remove_from_pool(&output->views, convert_to_wlc_handle(view));
   remove_from_pool(&output->mutable, convert_to_wlc_handle(view));
   wlc_output_schedule_repaint(output);
}

void
wlc_output_link_view(struct wlc_output *output, struct wlc_view *view, enum output_link link, struct wlc_view *other)
{
   assert(view);

   if (!output)
      return;

   struct wlc_output *old;
   if ((old = wlc_view_get_output_ptr(view))) {
      remove_from_pool(&old->views, convert_to_wlc_handle(view));
      if (old != output)
         remove_from_pool(&old->mutable, convert_to_wlc_handle(view));
   }

   bool added = false;
   wlc_handle handle = convert_to_wlc_handle(view);

   if (other) {
      wlc_handle *h, otherh = convert_to_wlc_handle(other);
      chck_iter_pool_for_each(&output->views, h) {
         if (*h != otherh)
            continue;

         added = chck_iter_pool_insert(&output->views, (link == LINK_ABOVE ? _I : _I - 1), &handle);
         break;
      }
   } else {
      switch (link) {
         case LINK_ABOVE:
            added = chck_iter_pool_push_back(&output->views, &handle);
            break;

         case LINK_BELOW:
            added = chck_iter_pool_push_front(&output->views, &handle);
            break;
      }
   }

   if (!exists_in_pool(&output->mutable, handle))
      chck_iter_pool_push_back(&output->mutable, &handle);

   if (old != output && view->state.created)
      WLC_INTERFACE_EMIT(view.move_to_output, convert_to_wlc_handle(view), convert_to_wlc_handle(old), (added ? convert_to_wlc_handle(output) : 0));

   if (!added)
      return;

   attach_view(output, view);
   wlc_output_schedule_repaint(output);
}

void
wlc_output_set_resolution_ptr(struct wlc_output *output, const struct wlc_size *resolution)
{
   if (!output)
      return;

   assert(resolution && resolution->w != 0 && resolution->h != 0);

   if (resolution->w == 0 || resolution->h == 0) {
      wlc_log(WLC_LOG_WARN, "Tried to set resolution of output %" PRIuWLC "to %ux%u", convert_to_wlc_handle(output), resolution->w, resolution->h);
      return;
   }

   if (wlc_size_equals(resolution, &output->resolution))
      return;

   size_t gsz;
   if (chck_mul_ofsz(resolution->w, resolution->h, &gsz)) {
      wlc_log(WLC_LOG_WARN, "Requested resolution %ux%u overflows when multiplied, ignoring resolution", resolution->w, resolution->h);
      return;
   }

   free(output->blit);
   except(output->blit = calloc(1, gsz));

   struct wlc_size old = output->resolution;
   output->resolution = *resolution;
   WLC_INTERFACE_EMIT(output.resolution, convert_to_wlc_handle(output), &old, &output->resolution);
   wlc_output_schedule_repaint(output);
}

void
wlc_output_set_sleep_ptr(struct wlc_output *output, bool sleep)
{
   if (!output)
      return;

   if (output->state.sleeping == sleep)
      return;

   if (sleep && output->state.pending) {
      output->task.sleep = true;
      return;
   }

   if (output->bsurface.api.sleep)
      output->bsurface.api.sleep(&output->bsurface, sleep);

   if (!(output->state.sleeping = sleep)) {
      wlc_output_schedule_repaint(output);
      wlc_log(WLC_LOG_INFO, "Output (%p) wake up", output);
   } else {
      if (output->bsurface.api.sleep) {
         // we fake sleep otherwise, by just drawing black
         cancel_repaint(output);
      }
      output->state.scheduled = output->state.activity = false;
      wlc_log(WLC_LOG_INFO, "Output (%p) sleep", output);
   }
}

void
wlc_output_set_mask_ptr(struct wlc_output *output, uint32_t mask)
{
   if (!output)
      return;

   output->active.mask = mask;
   wlc_output_schedule_repaint(output);
}

void
wlc_output_get_pixels_ptr(struct wlc_output *output, bool (*pixels)(const struct wlc_size *size, uint8_t *rgba, void *arg), void *arg)
{
   assert(pixels);

   if (!output)
      return;

   // TODO: we need real task system, not like we do right now.
   output->task.pixels.cb = pixels;
   output->task.pixels.arg = arg;
   wlc_output_schedule_repaint(output);
}

bool
wlc_output_set_views_ptr(struct wlc_output *output, const wlc_handle *views, size_t memb)
{
   if (!output || !chck_iter_pool_set_c_array(&output->views, views, memb) || !chck_iter_pool_set_c_array(&output->mutable, views, memb))
      return false;

   wlc_handle *h;
   chck_iter_pool_for_each(&output->views, h)
      attach_view(output, convert_from_wlc_handle(*h, "view"));

   wlc_output_schedule_repaint(output);
   return true;
}

const wlc_handle*
wlc_output_get_views_ptr(struct wlc_output *output, size_t *out_memb)
{
   if (out_memb)
      *out_memb = 0;

   return (output ? chck_iter_pool_to_c_array(&output->views, out_memb) : NULL);
}

wlc_handle*
wlc_output_get_mutable_views_ptr(struct wlc_output *output, size_t *out_memb)
{
   if (out_memb)
      *out_memb = 0;

   return (output ? chck_iter_pool_to_c_array(&output->mutable, out_memb) : NULL);
}

void
wlc_output_focus_ptr(struct wlc_output *output)
{
   struct wlc_focus_event ev = { .output = output, .type = WLC_FOCUS_EVENT_OUTPUT };
   wl_signal_emit(&wlc_system_signals()->focus, &ev);
}

static void*
get(struct wlc_output *output, size_t offset)
{
   return (output ? ((char*)output + offset) : NULL);
}

WLC_API const struct wlc_size*
wlc_output_get_resolution(wlc_handle output)
{
   return get(convert_from_wlc_handle(output, "output"), offsetof(struct wlc_output, resolution));
}

WLC_API void
wlc_output_set_resolution(wlc_handle output, const struct wlc_size *resolution)
{
   wlc_output_set_resolution_ptr(convert_from_wlc_handle(output, "output"), resolution);
}

WLC_API bool
wlc_output_get_sleep(wlc_handle output)
{
   void *ptr = get(convert_from_wlc_handle(output, "output"), offsetof(struct wlc_output, state.sleeping));
   return (ptr ? *(bool*)ptr : false);
}

WLC_API void
wlc_output_set_sleep(wlc_handle output, bool sleep)
{
   wlc_output_set_sleep_ptr(convert_from_wlc_handle(output, "output"), sleep);
}

WLC_API uint32_t
wlc_output_get_mask(wlc_handle output)
{
   void *ptr = get(convert_from_wlc_handle(output, "output"), offsetof(struct wlc_output, active.mask));
   return (ptr ? *(uint32_t*)ptr : 0);
}

WLC_API void
wlc_output_set_mask(wlc_handle output, uint32_t mask)
{
   wlc_output_set_mask_ptr(convert_from_wlc_handle(output, "output"), mask);
}

WLC_API void
wlc_output_get_pixels(wlc_handle output, bool (*pixels)(const struct wlc_size *size, uint8_t *rgba, void *arg), void *arg)
{
   wlc_output_get_pixels_ptr(convert_from_wlc_handle(output, "output"), pixels, arg);
}

WLC_API const wlc_handle*
wlc_output_get_views(wlc_handle output, size_t *out_memb)
{
   return wlc_output_get_views_ptr(convert_from_wlc_handle(output, "output"), out_memb);
}

WLC_API wlc_handle*
wlc_output_get_mutable_views(wlc_handle output, size_t *out_memb)
{
   return wlc_output_get_mutable_views_ptr(convert_from_wlc_handle(output, "output"), out_memb);
}

WLC_API bool
wlc_output_set_views(wlc_handle output, const wlc_handle *views, size_t memb)
{
   return wlc_output_set_views_ptr(convert_from_wlc_handle(output, "output"), views, memb);
}

WLC_API void
wlc_output_focus(wlc_handle output)
{
   wlc_output_focus_ptr(convert_from_wlc_handle(output, "output"));
}

WLC_API const char*
wlc_output_get_name(wlc_handle output)
{
   struct wlc_output *o = convert_from_wlc_handle(output, "output");
   return (o ? o->information.name.data : NULL);
}

WLC_API enum wlc_connector_type
wlc_output_get_connector_type(wlc_handle output)
{
   struct wlc_output *o = convert_from_wlc_handle(output, "output");
   return (o ? o->information.connector : WLC_CONNECTOR_UNKNOWN);
}

WLC_API uint32_t
wlc_output_get_connector_id(wlc_handle output)
{
   struct wlc_output *o = convert_from_wlc_handle(output, "output");
   return (o ? o->information.connector_id : 0);
}

void
wlc_output_terminate(struct wlc_output *output)
{
   assert(output);

   if (output->state.pending) {
      output->task.terminate = true;
      wlc_log(WLC_LOG_INFO, "Terminating output (%" PRIuWLC ")...", convert_to_wlc_handle(output));
      wlc_output_schedule_repaint(output);
      return;
   }

   wlc_log(WLC_LOG_INFO, "Output (%" PRIuWLC ") terminated...", convert_to_wlc_handle(output));
   struct wlc_output_event ev = { .remove = { .output = output }, .type = WLC_OUTPUT_EVENT_REMOVE };
   wl_signal_emit(&wlc_system_signals()->output, &ev);
}

void
wlc_output_release(struct wlc_output *output)
{
   if (!output)
      return;

   if (output->timer.idle)
      wl_event_source_remove(output->timer.idle);

   wlc_output_set_information(output, NULL);
   wlc_output_set_backend_surface(output, NULL);
   chck_iter_pool_release(&output->surfaces);
   chck_iter_pool_release(&output->views);
   chck_iter_pool_release(&output->mutable);
   chck_iter_pool_release(&output->visible);
   chck_iter_pool_release(&output->callbacks);

   free(output->blit);
   output->blit = NULL;

   if (output->wl.output)
      wl_global_destroy(output->wl.output);

   wlc_source_release(&output->resources);
}

bool
wlc_output(struct wlc_output *output)
{
   assert(output);

   if (!(output->timer.idle = wl_event_loop_add_timer(wlc_event_loop(), cb_idle_timer, (void*)convert_to_wlc_handle(output))))
      goto fail;

   if (!(output->wl.output = wl_global_create(wlc_display(), &wl_output_interface, 2, output, wl_output_bind)))
      goto fail;

   if (!wlc_source(&output->resources, "output", NULL, NULL, 32, sizeof(struct wlc_resource)))
      goto fail;

   if (!chck_iter_pool(&output->surfaces, 32, 0, sizeof(wlc_resource)) ||
       !chck_iter_pool(&output->views, 4, 0, sizeof(wlc_handle)) ||
       !chck_iter_pool(&output->mutable, 4, 0, sizeof(wlc_handle)) ||
       !chck_iter_pool(&output->callbacks, 32, 0, sizeof(wlc_resource)) ||
       !chck_iter_pool(&output->visible, 32, 0, sizeof(struct wlc_view*)))
      goto fail;

   output->active.mode = UINT_MAX;
   output->state.ims = 41;
   const char *bg = getenv("WLC_BG");
   output->options.enable_bg = (chck_cstreq(bg, "0") ? false : true);

   wlc_output_set_sleep_ptr(output, false);
   wlc_output_set_mask_ptr(output, (1<<0));
   return true;

fail:
   wlc_output_release(output);
   return false;
}
