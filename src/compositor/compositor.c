#include <assert.h>
#include "internal.h"
#include "macros.h"
#include "visibility.h"
#include "compositor.h"
#include "output.h"
#include "view.h"
#include "session/tty.h"
#include "resources/resources.h"
#include "resources/types/region.h"
#include "resources/types/surface.h"

static void
wl_cb_subsurface_set_position(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y)
{
   (void)client, (void)resource, (void)x, (void)y;
   STUBL(resource);
}

static void
wl_cb_subsurface_place_above(struct wl_client *client, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   (void)client, (void)resource, (void)sibling_resource;
   STUBL(resource);
}

static void
wl_cb_subsurface_place_below(struct wl_client *client, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   (void)client, (void)resource, (void)sibling_resource;
   STUBL(resource);
}

static void
wl_cb_subsurface_set_sync(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   if (surface)
      surface->synchronized = true;
}

static void
wl_cb_subsurface_set_desync(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;

   struct wlc_surface *surface;
   if (!(surface = convert_from_wl_resource(resource, "surface")))
      return;

   if (surface)
      surface->synchronized = false;
}

static const struct wl_subsurface_interface wl_subsurface_implementation = {
   .destroy = wlc_cb_resource_destructor,
   .set_position = wl_cb_subsurface_set_position,
   .place_above = wl_cb_subsurface_place_above,
   .place_below = wl_cb_subsurface_place_below,
   .set_sync = wl_cb_subsurface_set_sync,
   .set_desync = wl_cb_subsurface_set_desync
};

static void
wl_cb_subcompositor_get_subsurface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource)
{
   struct wlc_compositor *compositor;
   if (!(compositor = wl_resource_get_user_data(resource)))
      return;

   wlc_resource surface = wlc_resource_from_wl_resource(surface_resource);
   wlc_resource parent = wlc_resource_from_wl_resource(parent_resource);

   if (surface == parent) {
      wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "wl_surface@%d cannot be its own parent", wl_resource_get_id(surface_resource));
      return;
   }

   wlc_resource r;
   if (!(r = wlc_resource_create(&compositor->subsurfaces, client, &wl_subsurface_interface, wl_resource_get_version(resource), 1, id)))
      return;

   wlc_resource_implement(r, &wl_subsurface_implementation, compositor);
   wlc_surface_set_parent(convert_from_wlc_resource(surface, "surface"), convert_from_wlc_resource(parent, "surface"));
}

static void
wl_cb_subcompositor_destroy(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   wl_resource_destroy(resource);
}

static const struct wl_subcompositor_interface wl_subcompositor_implementation = {
   .destroy = wl_cb_subcompositor_destroy,
   .get_subsurface = wl_cb_subcompositor_get_subsurface
};

static void
wl_subcompositor_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create_checked(client, &wl_subcompositor_interface, version, 1, id)))
      return;

   wl_resource_set_implementation(resource, &wl_subcompositor_implementation, data, NULL);
}

static void
wl_cb_surface_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wlc_compositor *compositor;
   if (!(compositor = wl_resource_get_user_data(resource)))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&compositor->surfaces, client, &wl_surface_interface, wl_resource_get_version(resource), 3, id)))
      return;

   wlc_resource_implement(r, &wl_surface_implementation, compositor);

   struct wlc_surface_event ev = { .surface = convert_from_wlc_resource(r, "surface"), .type = WLC_SURFACE_EVENT_CREATED };
   wl_signal_emit(&wlc_system_signals()->surface, &ev);
}

static void
wl_cb_region_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wlc_compositor *compositor;
   if (!(compositor = wl_resource_get_user_data(resource)))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&compositor->regions, client, &wl_region_interface, wl_resource_get_version(resource), 3, id)))
      return;

   wlc_resource_implement(r, &wl_region_implementation, wl_resource_get_user_data(resource));
}

static const struct wl_compositor_interface wl_compositor_implementation = {
   .create_surface = wl_cb_surface_create,
   .create_region = wl_cb_region_create
};

static void
wl_compositor_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *r;
   if (!(r = wl_resource_create_checked(client, &wl_compositor_interface, version, 3, id)))
      return;

   wl_resource_set_implementation(r, &wl_compositor_implementation, data, NULL);
}

static void
cb_idle_vt_switch(void *data)
{
   struct wlc_compositor *compositor = data;
   wlc_tty_activate_vt(compositor->state.vt);
   compositor->state.vt = 0;
   wl_event_source_remove(compositor->state.idle);
   compositor->state.idle = NULL;
}

static void
activate_tty(struct wlc_compositor *compositor)
{
   if (compositor->state.tty != ACTIVATING)
      return;

   compositor->state.tty = IDLE;
   wlc_tty_activate();
}

static void
deactivate_tty(struct wlc_compositor *compositor)
{
   if (compositor->state.tty != DEACTIVATING)
      return;

   // check that all outputs are surfaceless
   struct wlc_output *o;
   chck_pool_for_each(&compositor->outputs.pool, o) {
      if (o->bsurface.display)
         return;
   }

   compositor->state.tty = IDLE;

   if (compositor->state.vt != 0) {
      compositor->state.idle = wl_event_loop_add_idle(wlc_event_loop(), cb_idle_vt_switch, compositor);
   } else {
      wlc_tty_deactivate();
   }
}

static void
respond_tty_activate(struct wlc_compositor *compositor)
{
   if (compositor->state.tty == ACTIVATING) {
      activate_tty(compositor);
   } else if (compositor->state.tty == DEACTIVATING) {
      deactivate_tty(compositor);
   }
}

static void
activate_event(struct wl_listener *listener, void *data)
{
   struct wlc_compositor *compositor;
   except(compositor = wl_container_of(listener, compositor, listener.activate));

   struct wlc_activate_event *ev = data;
   if (!ev->active) {
      compositor->state.tty = DEACTIVATING;
      compositor->state.vt = ev->vt;
      chck_pool_for_each_call(&compositor->outputs.pool, wlc_output_set_backend_surface, NULL);
      deactivate_tty(compositor);
   } else {
      compositor->state.tty = ACTIVATING;
      compositor->state.vt = 0;
      activate_tty(compositor);
      wlc_backend_update_outputs(&compositor->backend, &compositor->outputs.pool);
      chck_pool_for_each_call(&compositor->outputs.pool, wlc_output_set_sleep_ptr, false);
   }
}

static void
terminate_event(struct wl_listener *listener, void *data)
{
   (void)data;
   struct wlc_compositor *compositor;
   except(compositor = wl_container_of(listener, compositor, listener.terminate));
   wlc_compositor_terminate(compositor);
}

static void
xwayland_event(struct wl_listener *listener, void *data)
{
   bool activated = *(bool*)data;

   struct wlc_compositor *compositor;
   except(compositor = wl_container_of(listener, compositor, listener.xwayland));

   if (activated) {
      wlc_xwm(&compositor->xwm);
   } else {
      wlc_xwm_release(&compositor->xwm);
   }
}

static void
attach_surface_to_view_or_create(struct wlc_compositor *compositor, struct wlc_surface *surface, enum wlc_shell_surface_type type, wlc_resource shell_surface)
{
   assert(compositor && surface && type >= 0 && type < WLC_SHELL_SURFACE_TYPE_LAST);

   struct wlc_view *view;
   if (!(view = wlc_compositor_view_for_surface(compositor, surface)))
      return;

   wlc_resource *res[WLC_SHELL_SURFACE_TYPE_LAST] = {
      &view->shell_surface,
      &view->xdg_surface,
   };

   const char *name[WLC_SHELL_SURFACE_TYPE_LAST] = {
      "shell-surface",
      "xdg-surface",
   };

   *res[type] = shell_surface;
   wl_resource_set_user_data(wl_resource_from_wlc_resource(shell_surface, name[type]), (void*)convert_to_wlc_handle(view));
}

static void
attach_popup_to_view_or_create(struct wlc_compositor *compositor, struct wlc_surface *surface, struct wlc_surface *parent, struct wlc_origin *origin, wlc_resource resource)
{
   assert(compositor && surface && parent);

   struct wlc_view *view;
   if (!(view = wlc_compositor_view_for_surface(compositor, surface)))
      return;

   view->xdg_popup = resource;
   view->pending.geometry.origin = *origin;
   wlc_view_set_parent_ptr(view, convert_from_wlc_handle(parent->view, "view"));
   wlc_view_set_type_ptr(view, WLC_BIT_POPUP, true);
   wl_resource_set_user_data(wl_resource_from_wlc_resource(resource, "xdg-popup"), (void*)convert_to_wlc_handle(view));
}

static void
surface_event(struct wl_listener *listener, void *data)
{
   struct wlc_compositor *compositor;
   except(compositor = wl_container_of(listener, compositor, listener.surface));

   struct wlc_surface_event *ev = data;
   switch (ev->type) {
      case WLC_SURFACE_EVENT_REQUEST_VIEW_ATTACH:
         attach_surface_to_view_or_create(compositor, ev->surface, ev->attach.type, ev->attach.shell_surface);
      break;

      case WLC_SURFACE_EVENT_REQUEST_VIEW_POPUP:
         attach_popup_to_view_or_create(compositor, ev->surface, ev->popup.parent, &ev->popup.origin, ev->popup.resource);
      break;

      case WLC_SURFACE_EVENT_DESTROYED:
         {
            struct wlc_view *v;
            chck_pool_for_each(&compositor->views.pool, v) {
               if (v->parent == ev->surface->view)
                  wlc_view_set_parent_ptr(v, NULL);
            }

            struct wlc_surface *s;
            chck_pool_for_each(&compositor->surfaces.pool, s) {
               if (s->parent == convert_to_wlc_resource(ev->surface))
                  wlc_surface_set_parent(s, NULL);
            }
         }
      break;

      default:
      break;
   }
}

static struct wlc_output*
get_surfaceless_output(struct wlc_compositor *compositor)
{
   struct wlc_output *o;
   chck_pool_for_each(&compositor->outputs.pool, o) {
      if (!o->bsurface.display)
         return o;
   }
   return NULL;
}

static void
active_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   assert(compositor);

   wlc_dlog(WLC_DBG_FOCUS, "focus output %zu %zu", compositor->active.output, convert_to_wlc_handle(output));

   if (compositor->active.output == convert_to_wlc_handle(output))
      return;

   if (compositor->active.output)
      WLC_INTERFACE_EMIT(output.focus, compositor->active.output, false);

   wlc_output_schedule_repaint(convert_from_wlc_handle(compositor->active.output, "output"));
   compositor->active.output = convert_to_wlc_handle(output);

   if (compositor->active.output) {
      WLC_INTERFACE_EMIT(output.focus, compositor->active.output, true);
      wlc_output_schedule_repaint(output);
   }
}

static void
add_output(struct wlc_compositor *compositor, struct wlc_backend_surface *bsurface, struct wlc_output_information *info)
{
   assert(compositor && bsurface && info);

   struct wlc_output *output;
   if (!(output = get_surfaceless_output(compositor)) && (output = wlc_handle_create(&compositor->outputs)))
      WLC_INTERFACE_EMIT(output.created, convert_to_wlc_handle(output));

   if (!output) {
      wlc_backend_surface_release(bsurface);
      return;
   }

   wlc_output_set_information(output, info);
   wlc_output_set_backend_surface(output, bsurface);

   if (!compositor->active.output)
      active_output(compositor, output);

   wlc_output_schedule_repaint(output);
   wlc_log(WLC_LOG_INFO, "Added output (%zu)", convert_to_wlc_handle(output));
}

static void
remove_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   assert(compositor && output);

   struct wlc_output *o, *alive = NULL;
   chck_pool_for_each(&compositor->outputs.pool, o) {
      if (!o->bsurface.display || o == output)
         continue;

      alive = o;
      break;
   }

   if (compositor->active.output == convert_to_wlc_handle(output)) {
      compositor->active.output = 0; // make sure we don't redraw
      active_output(compositor, alive);
   }

   WLC_INTERFACE_EMIT(output.destroyed, convert_to_wlc_handle(output));
   wlc_output_set_backend_surface(output, NULL);

   wlc_log(WLC_LOG_INFO, "Removed output (%zu)", convert_to_wlc_handle(output));

   if (compositor->state.terminating && !alive)
      wlc_compositor_terminate(compositor);
}

static void
output_event(struct wl_listener *listener, void *data)
{
   struct wlc_output_event *ev = data;

   struct wlc_compositor *compositor;
   except(compositor = wl_container_of(listener, compositor, listener.output));

   switch (ev->type) {
      case WLC_OUTPUT_EVENT_ADD:
         add_output(compositor, ev->add.bsurface, ev->add.info);
      break;

      case WLC_OUTPUT_EVENT_ACTIVE:
         active_output(compositor, ev->active.output);
      break;

      case WLC_OUTPUT_EVENT_REMOVE:
         remove_output(compositor, ev->remove.output);
      break;

      case WLC_OUTPUT_EVENT_UPDATE:
         wlc_backend_update_outputs(&compositor->backend, &compositor->outputs.pool);
      break;

      case WLC_OUTPUT_EVENT_SURFACE:
         respond_tty_activate(compositor);
      break;
   }
}

static void
focus_event(struct wl_listener *listener, void *data)
{
   struct wlc_compositor *compositor;
   except(compositor = wl_container_of(listener, compositor, listener.focus));

   struct wlc_focus_event *ev = data;
   switch (ev->type) {
      case WLC_FOCUS_EVENT_OUTPUT:
         active_output(compositor, ev->output);
      break;

      default:break;
   }
}

struct wlc_view*
wlc_compositor_view_for_surface(struct wlc_compositor *compositor, struct wlc_surface *surface)
{
   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle(surface->view, "view")) && !(view = wlc_handle_create(&compositor->views)))
      return NULL;

   wlc_surface_attach_to_view(surface, view);

   struct wlc_output *output;
   if ((output = convert_from_wlc_handle(compositor->active.output, "output"))) {
      wlc_view_set_output_ptr(view, output);
      wlc_view_set_mask_ptr(view, output->active.mask);
   }

   return view;
}

// XXX: We do not currently expose compositor to public API.
//      So we use static variable here for some public api functions.
//
//      Never use this variable anywhere else.
static struct wlc_compositor *_g_compositor;

WLC_API const wlc_handle*
wlc_get_outputs(size_t *out_memb)
{
   assert(_g_compositor);

   if (out_memb)
      *out_memb = 0;

   // Allocate linear array which we then return
   free(_g_compositor->tmp.outputs);
   if (!(_g_compositor->tmp.outputs = malloc(_g_compositor->outputs.pool.items.count * sizeof(wlc_handle))))
      return NULL;

   {
      size_t i = 0;
      struct wlc_output *o;
      chck_pool_for_each(&_g_compositor->outputs.pool, o)
         _g_compositor->tmp.outputs[i++] = convert_to_wlc_handle(o);
   }

   if (out_memb)
      *out_memb = _g_compositor->outputs.pool.items.count;

   return _g_compositor->tmp.outputs;
}

WLC_API wlc_handle
wlc_get_focused_output(void)
{
   assert(_g_compositor);
   return _g_compositor->active.output;
}

void
wlc_compositor_terminate(struct wlc_compositor *compositor)
{
   if (!compositor || !_g_compositor)
      return;

   if (!compositor->state.terminating) {
      wlc_log(WLC_LOG_INFO, "Terminating compositor...");
      compositor->state.terminating = true;

      if (compositor->outputs.pool.items.count > 0) {
         chck_pool_for_each_call(&compositor->outputs.pool, wlc_output_terminate);
         return;
      }
   }

   wlc_log(WLC_LOG_INFO, "Compositor terminated...");
   wl_signal_emit(&wlc_system_signals()->compositor, NULL);
}

void
wlc_compositor_release(struct wlc_compositor *compositor)
{
   if (!compositor || !_g_compositor)
      return;

   wl_list_remove(&compositor->listener.activate.link);
   wl_list_remove(&compositor->listener.terminate.link);
   wl_list_remove(&compositor->listener.xwayland.link);
   wl_list_remove(&compositor->listener.surface.link);
   wl_list_remove(&compositor->listener.output.link);
   wl_list_remove(&compositor->listener.focus.link);

   wlc_xwm_release(&compositor->xwm);
   wlc_backend_release(&compositor->backend);
   wlc_shell_release(&compositor->shell);
   wlc_seat_release(&compositor->seat);

   if (compositor->wl.subcompositor)
      wl_global_destroy(compositor->wl.subcompositor);

   if (compositor->wl.compositor)
      wl_global_destroy(compositor->wl.compositor);

   free(_g_compositor->tmp.outputs);
   wlc_source_release(&compositor->outputs);
   wlc_source_release(&compositor->views);
   wlc_source_release(&compositor->surfaces);
   wlc_source_release(&compositor->regions);

   memset(compositor, 0, sizeof(struct wlc_compositor));
   _g_compositor = NULL;
}

bool
wlc_compositor(struct wlc_compositor *compositor)
{
   assert(wlc_display() && wlc_event_loop() && !_g_compositor);
   memset(compositor, 0, sizeof(struct wlc_compositor));

   if (!wlc_display() || !wlc_event_loop() || _g_compositor) {
      wlc_log(WLC_LOG_ERROR, "wlc_compositor called before wlc_init()");
      abort();
   }

   _g_compositor = compositor;

   compositor->listener.activate.notify = activate_event;
   compositor->listener.terminate.notify = terminate_event;
   compositor->listener.xwayland.notify = xwayland_event;
   compositor->listener.surface.notify = surface_event;
   compositor->listener.output.notify = output_event;
   compositor->listener.focus.notify = focus_event;
   wl_signal_add(&wlc_system_signals()->activate, &compositor->listener.activate);
   wl_signal_add(&wlc_system_signals()->terminate, &compositor->listener.terminate);
   wl_signal_add(&wlc_system_signals()->xwayland, &compositor->listener.xwayland);
   wl_signal_add(&wlc_system_signals()->surface, &compositor->listener.surface);
   wl_signal_add(&wlc_system_signals()->output, &compositor->listener.output);
   wl_signal_add(&wlc_system_signals()->focus, &compositor->listener.focus);

   if (!wlc_source(&compositor->outputs, "output", wlc_output, wlc_output_release, 4, sizeof(struct wlc_output)) ||
       !wlc_source(&compositor->views, "view", wlc_view, wlc_view_release, 32, sizeof(struct wlc_view)) ||
       !wlc_source(&compositor->surfaces, "surface", wlc_surface, wlc_surface_release, 32, sizeof(struct wlc_surface)) ||
       !wlc_source(&compositor->regions, "region", NULL, wlc_region_release, 32, sizeof(struct wlc_region)))
      goto fail;

   if (!(compositor->wl.compositor = wl_global_create(wlc_display(), &wl_compositor_interface, 3, compositor, wl_compositor_bind)))
      goto compositor_interface_fail;

   if (!(compositor->wl.subcompositor = wl_global_create(wlc_display(), &wl_subcompositor_interface, 1, compositor, wl_subcompositor_bind)))
      goto subcompositor_interface_fail;

   if (!wlc_seat(&compositor->seat) ||
       !wlc_shell(&compositor->shell) ||
       !wlc_xdg_shell(&compositor->xdg_shell) ||
       !wlc_backend(&compositor->backend))
      goto fail;

   return true;

compositor_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind compositor interface");
   goto fail;
subcompositor_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind subcompositor interface");
   goto fail;
fail:
   wlc_compositor_release(compositor);
   return false;
}
