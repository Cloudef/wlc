#define _POSIX_C_SOURCE 200809L
#include "wlc.h"
#include "wlc_internal.h"
#include "compositor.h"
#include "visibility.h"
#include "callback.h"
#include "view.h"
#include "surface.h"
#include "region.h"
#include "output.h"
#include "macros.h"

#include "data-device/manager.h"

#include "seat/seat.h"
#include "seat/pointer.h"
#include "seat/client.h"

#include "shell/shell.h"
#include "shell/xdg-shell.h"

#include "backend/backend.h"
#include "context/context.h"
#include "render/render.h"

#include "xwayland/xwayland.h"

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>

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
   (void)wl_client, (void)resource;
   STUBL(resource);
}

static void
wl_cb_subsurface_set_desync(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client, (void)resource;
   STUBL(resource);
}

static const struct wl_subsurface_interface wl_subsurface_implementation = {
   wl_cb_subsurface_destroy,
   wl_cb_subsurface_set_position,
   wl_cb_subsurface_place_above,
   wl_cb_subsurface_place_below,
   wl_cb_subsurface_set_sync,
   wl_cb_subsurface_set_desync
};

static void
wl_cb_subcompositor_get_subsurface(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource)
{
   (void)surface_resource, (void)parent_resource;
   STUBL(resource);

   struct wl_resource *subsurface_resource;
   if (!(subsurface_resource = wl_resource_create(wl_client, &wl_subsurface_interface, 1, id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   wl_resource_set_implementation(subsurface_resource, &wl_subsurface_implementation, NULL, NULL);
}

static void
wl_cb_subcompositor_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   wl_resource_destroy(resource);
}

static const struct wl_subcompositor_interface wl_subcompositor_implementation = {
   wl_cb_subcompositor_destroy,
   wl_cb_subcompositor_get_subsurface
};

static void
wl_subcompositor_bind(struct wl_client *wl_client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create(wl_client, &wl_compositor_interface, MIN(version, 1), id))) {
      wl_client_post_no_memory(wl_client);
      fprintf(stderr, "-!- failed create resource or bad version (%u > %u)\n", version, 1);
      return;
   }

   wl_resource_set_implementation(resource, &wl_subcompositor_implementation, data, NULL);
}

static void
wl_cb_surface_create(struct wl_client *wl_client, struct wl_resource *resource, unsigned int id)
{
   struct wlc_surface *surface = NULL;
   struct wl_resource *surface_resource;
   if (!(surface_resource = wl_resource_create(wl_client, &wl_surface_interface, 1, id)))
      goto fail;

   struct wlc_compositor *compositor = wl_resource_get_user_data(resource);
   if (!(surface = wlc_surface_new(compositor, compositor->output->space)))
      goto fail;

   struct wlc_client *client;
   if (!(client = wlc_client_for_client_with_wl_client_in_list(wl_client, &compositor->clients)))
      goto fail;

   struct wlc_view *view;
   if (!(view = wlc_view_new(client, surface)))
      goto fail;

   wl_list_insert(&compositor->unmapped, &view->link);
   wlc_surface_implement(surface, surface_resource);
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
   if (!(region_resource = wl_resource_create(wl_client, &wl_region_interface, 1, id)))
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
   wl_cb_surface_create,
   wl_cb_region_create
};

static void
wl_cb_compositor_client_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wl_client *wl_client = wl_resource_get_client(resource);
   struct wlc_compositor *compositor = wl_resource_get_user_data(resource);

   struct wlc_client *client;
   if ((client = wlc_client_for_client_with_wl_client_in_list(wl_client, &compositor->clients)))
      wlc_client_free(client);
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

   wl_list_insert(&compositor->clients, &client->link);

   struct wl_resource *resource;
   if (!(resource = wl_resource_create(wl_client, &wl_compositor_interface, MIN(version, 3), id))) {
      wl_client_post_no_memory(wl_client);
      fprintf(stderr, "-!- failed create resource or bad version (%u > %u)\n", version, 3);
      return;
   }

   wl_resource_set_implementation(resource, &wl_compositor_implementation, data, wl_cb_compositor_client_destructor);
}

static uint32_t
get_time(void)
{
   /* TODO: change to monotonic time */
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void
repaint(struct wlc_compositor *compositor)
{
   if (!wlc_is_active())
      return;

   uint32_t msec = get_time();

   struct wlc_output *output;
   wl_list_for_each(output, &compositor->outputs, link) {
      if (!compositor->render->api.bind(output))
         continue;

      compositor->render->api.clear();

      struct wlc_view *view;
      wl_list_for_each(view, &output->space->views, link) {
         if (!view->surface->created)
            continue;

         compositor->render->api.render(view);

         struct wlc_callback *cb, *cbn;
         wl_list_for_each_safe(cb, cbn, &view->surface->frame_cb_list, link) {
            wl_callback_send_done(cb->resource, msec);
            wl_resource_destroy(cb->resource);
         }
      }

      if (compositor->output == output)
         compositor->render->api.pointer(wl_fixed_to_int(compositor->seat->pointer->x), wl_fixed_to_int(compositor->seat->pointer->y));

      compositor->render->api.swap();
   }

   compositor->repaint_scheduled = false;
}

static void
cb_repaint_idle(void *data)
{
   repaint(data);
}

static void
schedule_repaint(struct wlc_compositor *compositor)
{
   if (compositor->repaint_scheduled)
      return;

   wl_event_loop_add_idle(compositor->event_loop, cb_repaint_idle, compositor);
   compositor->repaint_scheduled = true;
}

static int
cb_repaint_timer(void *data)
{
   schedule_repaint(data);
   return 1;
}

static void
resolution(struct wlc_compositor *compositor, struct wlc_output *output, uint32_t width, uint32_t height)
{
   if (output->resolution.width == width && output->resolution.height == height)
      return;

   output->resolution.width = width;
   output->resolution.height = height;

   if (compositor->render)
      compositor->render->api.resolution(output, width, height);

   if (compositor->interface.output.resolution)
      compositor->interface.output.resolution(compositor, output, width, height);

   schedule_repaint(compositor);
}

static void
active_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   if (output == compositor->output)
      return;

   compositor->output = output;

   if (compositor->interface.output.activated)
      compositor->interface.output.activated(compositor, output);

   schedule_repaint(compositor);
}

static bool
add_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   if (compositor->context && !compositor->context->api.attach(output))
      return false;

   wl_list_insert(&compositor->outputs, &output->link);

   if (compositor->interface.output.created)
      compositor->interface.output.created(compositor, output);

   if (!compositor->output)
      active_output(compositor, output);

   if (compositor->interface.output.resolution) {
      struct wlc_output_mode *mode;
      wl_array_for_each(mode, &output->information.modes) {
         if (!(mode->flags & WL_OUTPUT_MODE_CURRENT))
            continue;

         resolution(compositor, output, mode->width, mode->height);
         break;
      }
   }

   schedule_repaint(compositor);
   return true;
}

static void
remove_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   (void)compositor;
   wl_list_remove(&output->link);

   if (compositor->output == output) {
      struct wlc_output *o;
      compositor->output = (wl_list_empty(&compositor->outputs) ? NULL : wl_container_of(compositor->outputs.next, o, link));
   }

   struct wlc_space *space;
   wl_list_for_each(space, &output->spaces, link) {
      struct wlc_view *view, *vn;
      wl_list_for_each_safe(view, vn, &space->views, link) {
         wl_list_remove(&view->link);
         wl_list_insert(&compositor->unmapped, &view->link);

         if (compositor->render)
            compositor->render->api.destroy(view->surface);

         view->surface->created = false;

         if ((view->surface->space = (compositor->output ? compositor->output->space : NULL)))
            wlc_surface_create_notify(view->surface);
      }
   }

   if (compositor->interface.output.destroyed)
      compositor->interface.output.destroyed(compositor, output);

   schedule_repaint(compositor);
}

WLC_API void
wlc_compositor_focus_view(struct wlc_compositor *compositor, struct wlc_view *view)
{
   assert(compositor);
   compositor->seat->notify.keyboard_focus(compositor->seat, view);
   schedule_repaint(compositor);
}

WLC_API void
wlc_compositor_focus_output(struct wlc_compositor *compositor, struct wlc_output *output)
{
   assert(compositor);
   compositor->api.active_output(compositor, output);
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
wlc_compositor_run(struct wlc_compositor *compositor)
{
   assert(compositor);
   wl_display_run(compositor->display);
}

WLC_API void
wlc_compositor_free(struct wlc_compositor *compositor)
{
   assert(compositor);

   // FIXME: destroy xwm for this compositor

   if (compositor->repaint_timer)
      wl_event_source_remove(compositor->repaint_timer);

   if (compositor->render)
      wlc_render_terminate(compositor->render);

   if (compositor->context)
      wlc_context_terminate(compositor->context);

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

   if (compositor->display)
      wl_display_destroy(compositor->display);

   free(compositor);
}

WLC_API struct wlc_compositor*
wlc_compositor_new(const struct wlc_interface *interface)
{
   if (!wlc_has_init()) {
      fprintf(stderr, "-!- wlc_init() must be called before creating compositor. Doing otherwise might cause a security risk.\n");
      exit(EXIT_FAILURE);
   }

   struct wlc_compositor *compositor;
   if (!(compositor = calloc(1, sizeof(struct wlc_compositor))))
      goto out_of_memory;

   memcpy(&compositor->interface, interface, sizeof(struct wlc_interface));
   compositor->api.add_output = add_output;
   compositor->api.remove_output = remove_output;
   compositor->api.active_output = active_output;
   compositor->api.schedule_repaint = schedule_repaint;
   compositor->api.resolution = resolution;
   compositor->api.get_time = get_time;

   if (!(compositor->display = wl_display_create()))
      goto display_create_fail;

   const char *socket_name;
   if (!(socket_name = wl_display_add_socket_auto(compositor->display)))
      goto display_add_socket_fail;

   setenv("WAYLAND_DISPLAY", socket_name, true);

   wl_list_init(&compositor->clients);
   wl_list_init(&compositor->unmapped);
   wl_list_init(&compositor->outputs);

   if (!(compositor->global = wl_global_create(compositor->display, &wl_compositor_interface, 3, compositor, wl_compositor_bind)))
      goto compositor_interface_fail;

   if (!(compositor->global_sub = wl_global_create(compositor->display, &wl_subcompositor_interface, 1, compositor, wl_subcompositor_bind)))
      goto subcompositor_interface_fail;

   if (!(compositor->manager = wlc_data_device_manager_new(compositor)))
      goto fail;

   if (!(compositor->seat = wlc_seat_new(compositor)))
      goto fail;

   if (!(compositor->shell = wlc_shell_new(compositor)))
      goto fail;

   if (!(compositor->xdg_shell = wlc_xdg_shell_new(compositor)))
      goto fail;

   if (wl_display_init_shm(compositor->display) != 0)
      goto display_init_shm_fail;

   if (!(compositor->event_loop = wl_display_get_event_loop(compositor->display)))
      goto no_event_loop;

   if (!(compositor->backend = wlc_backend_init(compositor)))
      goto fail;

   if (!(compositor->context = wlc_context_init(compositor, compositor->backend)))
      goto fail;

   if (!(compositor->render = wlc_render_init(compositor->context)))
      goto fail;

   struct wlc_output *output;
   wl_list_for_each(output, &compositor->outputs, link) {
      compositor->context->api.attach(output);
      compositor->render->api.resolution(output, output->resolution.width, output->resolution.height);
   }

   // FIXME: do this on demand (when X client is spawned)
   // xwm should be compositor specific, xserver should be global
   if (!(wlc_xwayland_init(compositor)))
      exit(EXIT_FAILURE);

   compositor->repaint_timer = wl_event_loop_add_timer(compositor->event_loop, cb_repaint_timer, compositor);
   repaint(compositor);
   return compositor;

out_of_memory:
   fprintf(stderr, "-!- out of memory\n");
   goto fail;
display_create_fail:
   fprintf(stderr, "-!- failed to create wayland display\n");
   goto fail;
display_add_socket_fail:
   fprintf(stderr, "-!- failed to add socket to wayland display: %m\n");
   goto fail;
compositor_interface_fail:
   fprintf(stderr, "-!- failed to bind compositor interface\n");
   goto fail;
subcompositor_interface_fail:
   fprintf(stderr, "-!- failed to bind subcompositor interface\n");
   goto fail;
display_init_shm_fail:
   fprintf(stderr, "-!- failed to initialize shm display\n");
   goto fail;
no_event_loop:
   fprintf(stderr, "-!- display has no event loop\n");
   goto fail;
fail:
   if (compositor)
      wlc_compositor_free(compositor);
   return NULL;
}
