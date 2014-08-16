#include "compositor.h"
#include "visibility.h"
#include "callback.h"
#include "view.h"
#include "surface.h"
#include "region.h"
#include "macros.h"

#include "data-device/manager.h"

#include "seat/seat.h"
#include "seat/client.h"

#include "shell/shell.h"
#include "shell/xdg-shell.h"

#include "backend/backend.h"
#include "context/context.h"
#include "render/render.h"

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
   struct wlc_compositor *compositor = wl_resource_get_user_data(resource);

   struct wl_resource *surface_resource;
   if (!(surface_resource = wl_resource_create(wl_client, &wl_surface_interface, 1, id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   struct wlc_surface *surface;
   if (!(surface = wlc_surface_new(compositor))) {
      wl_resource_destroy(surface_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   struct wlc_client *client;
   if (!(client = wlc_client_for_client_with_wl_client_in_list(wl_client, &compositor->clients))) {
      wlc_surface_free(surface);
      wl_resource_destroy(surface_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   struct wlc_view *view;
   if (!(view = wlc_view_new(client, surface))) {
      wlc_surface_free(surface);
      wl_resource_destroy(surface_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   wl_list_insert(compositor->views.prev, &view->link);

   wlc_surface_implement(surface, surface_resource);
}

static void
wl_cb_region_create(struct wl_client *wl_client, struct wl_resource *resource, unsigned int id)
{
   struct wl_resource *region_resource;
   if (!(region_resource = wl_resource_create(wl_client, &wl_region_interface, 1, id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   struct wlc_region *region;
   if (!(region = wlc_region_new())) {
      wl_resource_destroy(region_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   wlc_region_implement(region, region_resource);
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

   puts("KILL CLIETN");
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
   uint32_t msec = get_time();

   struct wlc_view *view;
   wl_list_for_each(view, &compositor->views, link) {
      if (view->surface->frame_cb) {
         compositor->render->api.render(view);
         wl_callback_send_done(view->surface->frame_cb->resource, msec);
      }
   }

   compositor->render->api.swap();
   compositor->render->api.clear();

   wl_event_source_timer_update(compositor->repaint_timer, 16);
   compositor->repaint_scheduled = false;
}

static void
cb_repaint_idle(void *data)
{
   repaint(data);
}

static int
cb_repaint_timer(void *data)
{
   cb_repaint_idle(data);
   return 1;
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
poll_for_events(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;
   struct wlc_compositor *compositor = data;
   return compositor->backend->api.poll_events(compositor->seat);
}

WLC_API void
wlc_compositor_keyboard_focus(struct wlc_compositor *compositor, struct wlc_view *view)
{
   compositor->seat->notify.keyboard_focus(compositor->seat, view);
}

WLC_API void
wlc_compositor_inject(struct wlc_compositor *compositor, const struct wlc_interface *interface)
{
   memcpy(&compositor->interface, interface, sizeof(struct wlc_interface));
}

WLC_API void
wlc_compositor_run(struct wlc_compositor *compositor)
{
   wl_display_run(compositor->display);
}

WLC_API void
wlc_compositor_free(struct wlc_compositor *compositor)
{
   assert(compositor);

   if (compositor->repaint_timer)
      wl_event_source_remove(compositor->repaint_timer);

   if (compositor->event_source)
      wl_event_source_remove(compositor->event_source);

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
wlc_compositor_new(void)
{
   struct wlc_compositor *compositor;
   if (!(compositor = calloc(1, sizeof(struct wlc_compositor))))
      goto out_of_memory;

   if (!(compositor->display = wl_display_create()))
      goto display_create_fail;

   wl_list_init(&compositor->clients);
   wl_list_init(&compositor->views);

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

   if (wl_display_add_socket(compositor->display, NULL) != 0)
      goto display_add_socket_fail;

   if (!(compositor->event_loop = wl_display_get_event_loop(compositor->display)))
      goto no_event_loop;

   if (!(compositor->backend = wlc_backend_init()))
      goto fail;

   if (!(compositor->context = wlc_context_init(compositor->backend)))
      goto fail;

   if (!(compositor->render = wlc_render_init(compositor->context)))
      goto fail;

   if (compositor->backend->api.event_fd) {
      if (!(compositor->event_source = wl_event_loop_add_fd(compositor->event_loop, compositor->backend->api.event_fd(), WL_EVENT_READABLE, poll_for_events, compositor)))
         goto event_source_fail;

      wl_event_source_check(compositor->event_source);
   }

   compositor->repaint_timer = wl_event_loop_add_timer(compositor->event_loop, cb_repaint_timer, compositor);

   compositor->api.schedule_repaint = schedule_repaint;
   compositor->api.get_time = get_time;

   repaint(compositor);
   return compositor;

out_of_memory:
   fprintf(stderr, "-!- out of memory\n");
   goto fail;
display_create_fail:
   fprintf(stderr, "-!- failed to create wayland display\n");
   goto fail;
display_add_socket_fail:
   fprintf(stderr, "-!- failed to add socket to wayland display\n");
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
event_source_fail:
   fprintf(stderr, "-!- failed to add context event source\n");
fail:
   if (compositor)
      wlc_compositor_free(compositor);
   return NULL;
}
