#include "surface.h"
#include "macros.h"

#include "compositor/surface.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_shell_surface_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
   (void)client, (void)serial;
   STUB(resource);
}

static void
wl_cb_shell_surface_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial)
{
   (void)client, (void)resource, (void)seat, (void)serial;
   STUB(resource);
}

static void
wl_cb_shell_surface_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
   (void)client, (void)seat, (void)serial, (void)edges;
   STUB(resource);
}

static void
wl_cb_shell_surface_set_toplevel(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   STUB(resource);
}

static void
wl_cb_shell_surface_set_transient(struct wl_client *client, struct wl_resource *resource, struct wl_resource *parent, int32_t x, int32_t y, uint32_t flags)
{
   (void)client, (void)parent, (void)x, (void)y, (void)flags;
   STUB(resource);
}

static void
wl_cb_shell_surface_set_fullscreen(struct wl_client *client, struct wl_resource *resource, uint32_t method, uint32_t framerate, struct wl_resource *output_resource)
{
   (void)client, (void)method, (void)framerate;
   void *output = wl_resource_get_user_data(output_resource);
   struct wlc_shell_surface *shell_surface = wl_resource_get_user_data(resource);
   wlc_shell_surface_set_output(shell_surface, output);
   wlc_shell_surface_set_fullscreen(shell_surface, true);
}

static void
wl_cb_shell_surface_set_popup(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial, struct wl_resource *parent, int32_t x, int32_t y, uint32_t flags)
{
   (void)client, (void)seat, (void)serial, (void)parent, (void)x, (void)y, (void)flags;
   STUB(resource);
}

static void
wl_cb_shell_surface_set_maximized(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource)
{
   (void)client;
   void *output = wl_resource_get_user_data(output_resource);
   struct wlc_shell_surface *shell_surface = wl_resource_get_user_data(resource);
   wlc_shell_surface_set_output(shell_surface, output);
   wlc_shell_surface_set_maximized(shell_surface, true);
}

static void
wl_cb_shell_surface_set_title(struct wl_client *client, struct wl_resource *resource, const char *title)
{
   (void)client;
   struct wlc_shell_surface *shell_surface = wl_resource_get_user_data(resource);
   wlc_shell_surface_set_title(shell_surface, title);
}

static void
wl_cb_shell_surface_set_class(struct wl_client *client, struct wl_resource *resource, const char *class_)
{
   (void)client;
   struct wlc_shell_surface *shell_surface = wl_resource_get_user_data(resource);
   wlc_shell_surface_set_class(shell_surface, class_);
}

static const struct wl_shell_surface_interface wl_shell_surface_implementation = {
   wl_cb_shell_surface_pong,
   wl_cb_shell_surface_move,
   wl_cb_shell_surface_resize,
   wl_cb_shell_surface_set_toplevel,
   wl_cb_shell_surface_set_transient,
   wl_cb_shell_surface_set_fullscreen,
   wl_cb_shell_surface_set_popup,
   wl_cb_shell_surface_set_maximized,
   wl_cb_shell_surface_set_title,
   wl_cb_shell_surface_set_class,
};

static void
wl_cb_shell_surface_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wlc_shell_surface *shell_surface = wl_resource_get_user_data(resource);

   if (shell_surface) {
      shell_surface->resource = NULL;
      wlc_shell_surface_free(shell_surface);
   }
}

void
wlc_shell_surface_implement(struct wlc_shell_surface *shell_surface, struct wl_resource *resource)
{
   assert(shell_surface);

   if (shell_surface->resource == resource)
      return;

   if (shell_surface->resource)
      wl_resource_destroy(shell_surface->resource);

   shell_surface->resource = resource;
   wl_resource_set_implementation(shell_surface->resource, &wl_shell_surface_implementation, shell_surface, wl_cb_shell_surface_destructor);
}

void
wlc_shell_surface_set_parent(struct wlc_shell_surface *shell_surface, struct wlc_shell_surface *parent)
{
   shell_surface->parent = parent;
}

void
wlc_shell_surface_set_output(struct wlc_shell_surface *shell_surface, void *output)
{
   shell_surface->output = output;
}

void
wlc_shell_surface_set_title(struct wlc_shell_surface *shell_surface, const char *title)
{
   wlc_string_set(&shell_surface->title, title, true);
}

void
wlc_shell_surface_set_class(struct wlc_shell_surface *shell_surface, const char *class_)
{
   wlc_string_set(&shell_surface->class, class_, true);
}

void
wlc_shell_surface_set_fullscreen(struct wlc_shell_surface *shell_surface, bool fullscreen)
{
   shell_surface->fullscreen = fullscreen;
}

void
wlc_shell_surface_set_maximized(struct wlc_shell_surface *shell_surface, bool maximized)
{
   shell_surface->maximized = maximized;
}

void
wlc_shell_surface_free(struct wlc_shell_surface *shell_surface)
{
   assert(shell_surface);

   if (shell_surface->resource)
      wl_resource_destroy(shell_surface->resource);

   free(shell_surface);
}

struct wlc_shell_surface*
wlc_shell_surface_new(struct wlc_surface *surface)
{
   assert(surface);

   struct wlc_shell_surface *shell_surface;
   if (!(shell_surface = calloc(1, sizeof(struct wlc_shell_surface))))
      return NULL;

   shell_surface->surface = surface;
   return shell_surface;
}
