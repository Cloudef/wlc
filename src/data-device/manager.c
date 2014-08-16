#include "manager.h"
#include "macros.h"

#include "compositor/compositor.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_data_source_offer(struct wl_client *wl_client, struct wl_resource *resource, const char *type)
{
   (void)wl_client, (void)resource, (void)type;
   STUBL(resource);
}

static void
wl_cb_data_source_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client, (void)resource;
   wl_resource_destroy(resource);
}

static struct wl_data_source_interface wl_data_source_implementation = {
   wl_cb_data_source_offer,
   wl_cb_data_source_destroy
};

static void
wl_cb_manager_create_data_source(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id)
{
   (void)wl_client, (void)resource, (void)id;
   STUBL(resource);
}

static void
wl_cb_manager_get_data_device(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id, struct wl_resource *seat_resource)
{
   (void)seat_resource;

   struct wl_resource *device_resource;
   if (!(device_resource = wl_resource_create(wl_client, &wl_data_device_interface, 1, id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   wl_resource_set_implementation(resource, &wl_data_source_implementation, NULL, NULL);
}

static const struct wl_data_device_manager_interface wl_data_device_manager_implementation = {
   wl_cb_manager_create_data_source,
   wl_cb_manager_get_data_device
};

static void
wl_data_device_manager_bind(struct wl_client *wl_client, void *data, unsigned int version, unsigned int id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create(wl_client, &wl_data_device_manager_interface, MIN(version, 1), id))) {
      wl_client_post_no_memory(wl_client);
      fprintf(stderr, "-!- failed create resource or bad version (%u > %u)\n", version, 1);
      return;
   }

   wl_resource_set_implementation(resource, &wl_data_device_manager_implementation, data, NULL);
}

void
wlc_data_device_manager_free(struct wlc_data_device_manager *manager)
{
   assert(manager);

   if (manager->global)
      wl_global_destroy(manager->global);

   free(manager);
}

struct wlc_data_device_manager*
wlc_data_device_manager_new(struct wlc_compositor *compositor)
{
   struct wlc_data_device_manager *manager;
   if (!(manager = calloc(1, sizeof(struct wlc_data_device_manager))))
      goto out_of_memory;

   if (!(manager->global = wl_global_create(compositor->display, &wl_data_device_manager_interface, 1, manager, wl_data_device_manager_bind)))
      goto manager_interface_fail;

   manager->compositor = compositor;
   return manager;

out_of_memory:
   fprintf(stderr, "-!- out of memory\n");
   goto fail;
manager_interface_fail:
   fprintf(stderr, "-!- failed to bind data device manager interface\n");
fail:
   if (manager)
      wlc_data_device_manager_free(manager);
   return NULL;
}
