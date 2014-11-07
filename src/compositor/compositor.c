#include "internal.h"
#include "compositor.h"
#include "visibility.h"
#include "callback.h"
#include "view.h"
#include "surface.h"
#include "region.h"
#include "output.h"
#include "data.h"
#include "client.h"
#include "macros.h"

#include "seat/seat.h"
#include "seat/pointer.h"

#include "shell/shell.h"
#include "shell/xdg-shell.h"

#include "platform/backend/backend.h"

#include "xwayland/xwayland.h"
#include "xwayland/xwm.h"

#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void
wl_cb_subsurface_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   wl_resource_destroy(resource);
}

static void
wl_cb_subsurface_set_position(struct wl_client *wl_client, struct wl_resource *resource, int32_t x, int32_t y)
{
   (void)wl_client, (void)resource, (void)x, (void)y;
   STUBL(resource);
}

static void
wl_cb_subsurface_place_above(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   (void)wl_client, (void)resource, (void)sibling_resource;
   STUBL(resource);
}

static void
wl_cb_subsurface_place_below(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   (void)wl_client, (void)resource, (void)sibling_resource;
   STUBL(resource);
}

static void
wl_cb_subsurface_set_sync(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   if (surface)
      surface->synchronized = true;
}

static void
wl_cb_subsurface_set_desync(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   struct wlc_surface *surface = wl_resource_get_user_data(resource);

   if (surface)
      surface->synchronized = false;
}

static const struct wl_subsurface_interface wl_subsurface_implementation = {
   .destroy = wl_cb_subsurface_destroy,
   .set_position = wl_cb_subsurface_set_position,
   .place_above = wl_cb_subsurface_place_above,
   .place_below = wl_cb_subsurface_place_below,
   .set_sync = wl_cb_subsurface_set_sync,
   .set_desync = wl_cb_subsurface_set_desync
};

static void
wl_cb_subcompositor_get_subsurface(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource)
{
   struct wlc_surface *surface = wl_resource_get_user_data(surface_resource);
   struct wlc_surface *parent = wl_resource_get_user_data(parent_resource);

   if (surface == parent) {
      wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "wl_surface@%d cannot be its own parent", wl_resource_get_id(surface_resource));
      return;
   }

   struct wl_resource *subsurface_resource;
   if (!(subsurface_resource = wl_resource_create(wl_client, &wl_subsurface_interface, wl_resource_get_version(resource), id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   // surface->parent = parent;
   wl_resource_set_implementation(subsurface_resource, &wl_subsurface_implementation, surface, NULL);
}

static void
wl_cb_subcompositor_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   wl_resource_destroy(resource);
}

static const struct wl_subcompositor_interface wl_subcompositor_implementation = {
   .destroy = wl_cb_subcompositor_destroy,
   .get_subsurface = wl_cb_subcompositor_get_subsurface
};

static void
wl_subcompositor_bind(struct wl_client *wl_client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create(wl_client, &wl_subcompositor_interface, MIN(version, 1), id))) {
      wl_client_post_no_memory(wl_client);
      wlc_log(WLC_LOG_WARN, "Failed create resource or bad version (%u > %u)", version, 1);
      return;
   }

   wl_resource_set_implementation(resource, &wl_subcompositor_implementation, data, NULL);
}

static void
wl_cb_surface_create(struct wl_client *wl_client, struct wl_resource *resource, unsigned int id)
{
   struct wlc_surface *surface = NULL;
   struct wl_resource *surface_resource;
   if (!(surface_resource = wl_resource_create(wl_client, &wl_surface_interface, wl_resource_get_version(resource), id)))
      goto fail;

   if (!(surface = wlc_surface_new()))
      goto fail;

   wlc_surface_implement(surface, surface_resource);
   wl_signal_emit(&wlc_system_signals()->surface, wl_resource_get_user_data(resource));
   return;

fail:
   if (surface)
      wlc_surface_free(surface);
   if (surface_resource)
      wl_resource_destroy(surface_resource);
   wl_resource_post_no_memory(resource);
}

static void
wl_cb_region_create(struct wl_client *wl_client, struct wl_resource *resource, unsigned int id)
{
   struct wl_resource *region_resource;
   if (!(region_resource = wl_resource_create(wl_client, &wl_region_interface, wl_resource_get_version(resource), id)))
      goto fail;

   struct wlc_region *region;
   if (!(region = wlc_region_new()))
      goto fail;

   wlc_region_implement(region, region_resource);
   return;

fail:
   if (region_resource)
      wl_resource_destroy(region_resource);
   wl_resource_post_no_memory(resource);
}

static const struct wl_compositor_interface wl_compositor_implementation = {
   .create_surface = wl_cb_surface_create,
   .create_region = wl_cb_region_create
};

static void
wl_cb_compositor_client_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wl_client *wl_client = wl_resource_get_client(resource);
   struct wlc_compositor *compositor = wl_resource_get_user_data(resource);

   struct wlc_client *client;
   if ((client = wlc_client_for_client_with_wl_client_in_list(wl_client, &compositor->clients))) {
      client->wl_client = NULL;
      wlc_client_free(client);
   }
}

static void
wl_compositor_bind(struct wl_client *wl_client, void *data, unsigned int version, unsigned int id)
{
   struct wlc_compositor *compositor = data;

   struct wlc_client *client;
   if (!(client = wlc_client_new(wl_client))) {
      wl_client_post_no_memory(wl_client);
      return;
   }

   struct wl_resource *resource;
   if (!(resource = wl_resource_create(wl_client, &wl_compositor_interface, MIN(version, 3), id))) {
      client->wl_client = NULL;
      wlc_client_free(client);
      wl_client_post_no_memory(wl_client);
      wlc_log(WLC_LOG_WARN, "Failed create resource or bad version (%u > %u)", version, 3);
      return;
   }

   wl_resource_set_implementation(resource, &wl_compositor_implementation, data, wl_cb_compositor_client_destructor);
   wl_list_insert(&compositor->clients, &client->link);
}

static void
activated(struct wl_listener *listener, void *data)
{
   bool activated = *(bool*)data;
   struct wlc_compositor *compositor;

   if (!(compositor = wl_container_of(listener, compositor, listener.activated)))
      return;

   if (!activated)
      return;

   struct wlc_output *o;
   wl_list_for_each(o, &compositor->outputs, link)
      wlc_output_schedule_repaint(o);
}

static void
terminated(struct wl_listener *listener, void *data)
{
   (void)data;
   struct wlc_compositor *compositor;

   if (!(compositor = wl_container_of(listener, compositor, listener.terminated)))
      return;

   wlc_compositor_free(compositor);
}

static void
xwayland(struct wl_listener *listener, void *data)
{
   bool activated = *(bool*)data;
   struct wlc_compositor *compositor;

   if (!(compositor = wl_container_of(listener, compositor, listener.xwayland)))
      return;

   if (activated) {
      compositor->xwm = wlc_xwm_new(compositor);
   } else if (compositor->xwm) {
      wlc_xwm_free(compositor->xwm);
      compositor->xwm = NULL;
   }
}

static void
compositor_cleanup(struct wlc_compositor *compositor)
{
   assert(compositor);

   wl_list_remove(&compositor->listener.activated.link);
   wl_list_remove(&compositor->listener.terminated.link);
   wl_list_remove(&compositor->listener.xwayland.link);
   wl_list_remove(&compositor->listener.output.link);

   if (compositor->xwm)
      wlc_xwm_free(compositor->xwm);

   if (compositor->backend)
      wlc_backend_terminate(compositor->backend);

   if (compositor->xdg_shell)
      wlc_xdg_shell_free(compositor->xdg_shell);

   if (compositor->shell)
      wlc_shell_free(compositor->shell);

   if (compositor->seat)
      wlc_seat_free(compositor->seat);

   if (compositor->manager)
      wlc_data_device_manager_free(compositor->manager);

   if (compositor->global_sub)
      wl_global_destroy(compositor->global_sub);

   if (compositor->global)
      wl_global_destroy(compositor->global);

   free(compositor);
}

static void
active_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   if (output == compositor->output)
      return;

   if (compositor->output)
      wlc_output_schedule_repaint(compositor->output);

   compositor->output = output;
   WLC_INTERFACE_EMIT(output.activated, compositor, output);

   if (output)
      wlc_output_schedule_repaint(output);
}

static void
output_event(struct wl_listener *listener, void *data)
{
   struct wlc_output_event *ev = data;
   struct wlc_compositor *compositor;

   if (!(compositor = wl_container_of(listener, compositor, listener.output)))
      return;

   switch (ev->type) {
      case WLC_OUTPUT_EVENT_ADD:
         assert(ev->output);

         wl_list_insert(&compositor->outputs, &ev->output->link);
         WLC_INTERFACE_EMIT(output.created, compositor, ev->output);

         if (!compositor->output)
            active_output(compositor, ev->output);

         wlc_output_schedule_repaint(ev->output);
      break;

      case WLC_OUTPUT_EVENT_ACTIVE:
         active_output(compositor, ev->output);
      break;

      case WLC_OUTPUT_EVENT_REMOVE:
         assert(ev->output);
         wl_list_remove(&ev->output->link);

         if (compositor->output == ev->output) {
            compositor->output = NULL; // make sure we don't redraw
            struct wlc_output *o = (wl_list_empty(&compositor->outputs) ? NULL : wl_container_of(compositor->outputs.next, o, link));
            wlc_compositor_focus_output(compositor, o);
         }

         // XXX: keep list of surfaces and change their output instead
         struct wlc_space *space;
         wl_list_for_each(space, &ev->output->spaces, link) {
            struct wlc_view *view, *vn;
            wl_list_for_each_safe(view, vn, &space->views, link)
               wlc_view_set_space(view, (compositor->output ? compositor->output->space : NULL));
         }

         // FIXME: hack for now until above is made
         if (compositor->seat->pointer->surface && compositor->seat->pointer->surface->output == ev->output)
            wlc_surface_invalidate(compositor->seat->pointer->surface);

         WLC_INTERFACE_EMIT(output.destroyed, compositor, ev->output);

         // Remove surface from output
         // Destroys rendering context, etc...
         wlc_output_set_surface(ev->output, NULL);
      break;
   }
}

WLC_API void
wlc_compositor_focus_view(struct wlc_compositor *compositor, struct wlc_view *view)
{
   assert(compositor);
   compositor->seat->notify.keyboard_focus(compositor->seat, view);
}

WLC_API void
wlc_compositor_focus_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   assert(compositor);
   active_output(compositor, output);
}

WLC_API struct wl_list*
wlc_compositor_get_outputs(struct wlc_compositor *compositor)
{
   assert(compositor);
   return &compositor->outputs;
}

WLC_API struct wlc_output*
wlc_compositor_get_focused_output(struct wlc_compositor *compositor)
{
   assert(compositor);
   return compositor->output;
}

WLC_API struct wlc_space*
wlc_compositor_get_focused_space(struct wlc_compositor *compositor)
{
   assert(compositor);
   return (compositor->output ? compositor->output->space : NULL);
}

WLC_API void
wlc_compositor_free(struct wlc_compositor *compositor)
{
   assert(compositor);

   if (wl_list_empty(&compositor->outputs)) {
      compositor_cleanup(compositor);
   } else {
      struct wlc_output *o;
      wl_list_for_each(o, &compositor->outputs, link)
         wlc_output_terminate(o);
   }
}

WLC_API struct wlc_compositor*
wlc_compositor_new(void *userdata)
{
   if (!wlc_display() || !wlc_event_loop()) {
      wlc_log(WLC_LOG_ERROR, "wlc_init() must be called before creating compositor.");
      exit(EXIT_FAILURE);
   }

   struct wlc_compositor *compositor;
   if (!(compositor = calloc(1, sizeof(struct wlc_compositor))))
      goto out_of_memory;

   compositor->userdata = userdata;

   wl_list_init(&compositor->clients);
   wl_list_init(&compositor->outputs);

   compositor->listener.activated.notify = activated;
   compositor->listener.terminated.notify = terminated;
   compositor->listener.xwayland.notify = xwayland;
   compositor->listener.output.notify = output_event;
   wl_signal_add(&wlc_system_signals()->activated, &compositor->listener.activated);
   wl_signal_add(&wlc_system_signals()->terminated, &compositor->listener.terminated);
   wl_signal_add(&wlc_system_signals()->xwayland, &compositor->listener.xwayland);
   wl_signal_add(&wlc_system_signals()->output, &compositor->listener.output);

   if (!(compositor->global = wl_global_create(wlc_display(), &wl_compositor_interface, 3, compositor, wl_compositor_bind)))
      goto compositor_interface_fail;

   if (!(compositor->global_sub = wl_global_create(wlc_display(), &wl_subcompositor_interface, 1, compositor, wl_subcompositor_bind)))
      goto subcompositor_interface_fail;

   if (!(compositor->manager = wlc_data_device_manager_new(compositor)))
      goto fail;

   if (!(compositor->seat = wlc_seat_new(compositor)))
      goto fail;

   if (!(compositor->shell = wlc_shell_new(compositor)))
      goto fail;

   if (!(compositor->xdg_shell = wlc_xdg_shell_new(compositor)))
      goto fail;

   if (!(compositor->backend = wlc_backend_init(compositor)))
      goto fail;

   const char *bg = getenv("WLC_BG");
   compositor->options.enable_bg = (bg && !strcmp(bg, "0") ? false : true);
   return compositor;

out_of_memory:
   wlc_log(WLC_LOG_WARN, "Out of memory");
   goto fail;
compositor_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind compositor interface");
   goto fail;
subcompositor_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind subcompositor interface");
   goto fail;
fail:
   if (compositor)
      compositor_cleanup(compositor);
   return NULL;
}
