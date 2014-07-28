#include "compositor.h"
#include "surface.h"
#include "region.h"
#include "macros.h"
#include "context/egl.h"
#include "context/glx.h"
#include "render/gles2.h"

#include <stdio.h>
#include <stdlib.h>

static void
wl_cb_surface_create(struct wl_client *client, struct wl_resource *resource, unsigned int id)
{
   (void)client;

   struct wl_resource *surface_resource;
   if (!(surface_resource = wl_resource_create(client, &wl_surface_interface, 1, id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   struct wlc_surface *surface;
   if (!(surface = wlc_surface_new())) {
      wl_resource_destroy(surface_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   wlc_surface_implement(surface, surface_resource);
}

static void
wl_cb_region_create(struct wl_client *client, struct wl_resource *resource, unsigned int id)
{
   (void)client;

   struct wl_resource *region_resource;
   if (!(region_resource = wl_resource_create(client, &wl_region_interface, 1, id))) {
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
wl_compositor_bind(struct wl_client *client, void *data, unsigned int version, unsigned int id)
{
   (void)data;

   struct wl_resource *resource;
   if (!(resource = wl_resource_create(client, &wl_compositor_interface, MIN(version, 3), id))) {
      wl_client_post_no_memory(client);
      fprintf(stderr, "-!- failed create resource or bad version (%u > 3)", version);
      return;
   }

   wl_resource_set_implementation(resource, &wl_compositor_implementation, data, NULL);
}

void
wlc_compositor_run(struct wlc_compositor *compositor)
{
   wl_display_run(compositor->display);
}

void
wlc_compositor_free(struct wlc_compositor *compositor)
{
   wlc_gles2_terminate();
   wlc_glx_terminate();
   wlc_egl_terminate();

   if (compositor->shell)
      wlc_shell_free(compositor->shell);

   if (compositor->xdg_shell)
      wlc_xdg_shell_free(compositor->xdg_shell);

   if (compositor->display)
      wl_display_destroy(compositor->display);
}

struct wlc_compositor*
wlc_compositor_new(void)
{
   struct wlc_compositor *compositor;
   if (!(compositor = calloc(1, sizeof(struct wlc_compositor))))
      goto out_of_memory;

   if (!(compositor->display = wl_display_create()))
      goto display_create_fail;

   if (!wl_global_create(compositor->display, &wl_compositor_interface, 3, compositor, wl_compositor_bind))
      goto compositor_interface_fail;

   if (!(compositor->shell = wlc_shell_new(compositor->display, compositor)))
      goto fail;

   if (!(compositor->xdg_shell = wlc_xdg_shell_new(compositor->display, compositor)))
      goto fail;

   if (wl_display_init_shm(compositor->display) != 0)
      goto display_init_shm_fail;

   if (wl_display_add_socket(compositor->display, NULL) != 0)
      goto display_add_socket_fail;

   if (!(compositor->event_loop = wl_display_get_event_loop(compositor->display)))
      goto no_event_loop;

   bool use_glx = false;
   if (!wlc_egl_init(compositor->display)) {
      use_glx = true;
      if (!getenv("DISPLAY") || !wlc_glx_init(compositor->display))
         goto fail;
   }

   if (!wlc_gles2_init((use_glx ? wlc_glx_swap_buffers : wlc_egl_swap_buffers), use_glx))
      goto fail;

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
display_init_shm_fail:
   fprintf(stderr, "-!- failed to initialize shm display\n");
   goto fail;
no_event_loop:
   fprintf(stderr, "-!- display has no event loop\n");
fail:
   if (compositor)
      wlc_compositor_free(compositor);
   return NULL;
}
