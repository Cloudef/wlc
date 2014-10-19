#include "wlc.h"
#include "macros.h"
#include "manager.h"

#include "compositor/compositor.h"

#include "seat/client.h"
#include "seat/seat.h"

#include "types/string.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <wayland-server.h>

struct wlc_data_source {
   struct wl_resource *resource;
   struct wl_array types;
   struct wl_list offers;
};

static struct wlc_data_source*
wlc_data_source_new(struct wl_resource *resource)
{
   assert(resource);

   struct wlc_data_source *source;
   if (!(source = calloc(1, sizeof(struct wlc_data_source))))
      return NULL;

   source->resource = resource;
   wl_array_init(&source->types);
   wl_list_init(&source->offers);
   return source;
}

static void
wlc_data_source_free(struct wlc_data_source *source)
{
   assert(source);

   struct wlc_string *type;
   wl_array_for_each(type, &source->types)
      wlc_string_release(type);
   wl_array_release(&source->types);

   struct wl_resource *offer;
   wl_resource_for_each(offer, &source->offers) {
      wl_resource_set_user_data(offer, NULL);
      wl_resource_set_destructor(offer, NULL);
   }

   free(source);
}

static void
wl_cb_data_offer_accept(struct wl_client *wl_client, struct wl_resource *resource, uint32_t serial, const char *type)
{
   (void)wl_client, (void)serial;

   struct wlc_data_source *source;
   if (!(source = wl_resource_get_user_data(resource)))
      return;

   wl_data_source_send_target(source->resource, type);
}

static void
wl_cb_data_offer_receive(struct wl_client *wl_client, struct wl_resource *resource, const char *type, int fd)
{
   (void)wl_client;

   struct wlc_data_source *source;
   if (!(source = wl_resource_get_user_data(resource)))
      return;

    wl_data_source_send_send(source->resource, type, fd);
    close(fd);
}

static void
wl_cb_data_offer_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   wl_resource_destroy(resource);
}

static struct wl_data_offer_interface wl_data_offer_implementation = {
   .accept = wl_cb_data_offer_accept,
   .receive = wl_cb_data_offer_receive,
   .destroy = wl_cb_data_offer_destroy
};

static void
wl_cb_data_offer_destructor(struct wl_resource *offer_resource)
{
   wl_list_remove(wl_resource_get_link(offer_resource));
}

static void
wl_cb_data_source_offer(struct wl_client *wl_client, struct wl_resource *resource, const char *type)
{
   (void)wl_client, (void)resource, (void)type;
   struct wlc_data_source *source = wl_resource_get_user_data(resource);

   struct wlc_string *destination;
   if (!(destination = wl_array_add(&source->types, sizeof(struct wlc_string))))
      return;

   memset(destination, 0, sizeof(struct wlc_string));
   wlc_string_set(destination, type, true);
}

static void
wl_cb_data_source_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client, (void)resource;
   wl_resource_destroy(resource);
}

static struct wl_data_source_interface wl_data_source_implementation = {
   .offer = wl_cb_data_source_offer,
   .destroy = wl_cb_data_source_destroy
};

static void
wl_cb_data_source_destructor(struct wl_resource *source_resource)
{
   struct wlc_data_source *source = wl_resource_get_user_data(source_resource);
   wlc_data_source_free(source);
}

static void
wl_cb_manager_create_data_source(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id)
{
   struct wl_resource *source_resource;
   if (!(source_resource = wl_resource_create(wl_client, &wl_data_source_interface, wl_resource_get_version(resource), id)))
      goto fail;

   struct wlc_data_source *source;
   if (!(source = wlc_data_source_new(source_resource)))
      goto fail;

   wl_resource_set_implementation(source_resource, &wl_data_source_implementation, source, wl_cb_data_source_destructor);
   return;

fail:
   if (source_resource)
      wl_resource_destroy(source_resource);
   wl_resource_post_no_memory(resource);
}

static void
wl_cb_data_device_start_drag(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *source_resource, struct wl_resource *origin_resource, struct wl_resource *icon_resource, uint32_t serial)
{
   (void)wl_client, (void)resource, (void)source_resource, (void)origin_resource, (void)icon_resource, (void)serial;
   STUBL(resource);
}

static void
wl_cb_data_device_set_selection(struct wl_client *wl_client, struct wl_resource *resource, struct wl_resource *source_resource, uint32_t serial)
{
   (void)serial;

   struct wlc_data_device *device;
   if (!(device = wl_resource_get_user_data(resource)))
      return;

   if (source_resource == device->source_resource)
      return;

   if (device->source_resource) {
      wl_data_source_send_cancelled(device->source_resource);
      wl_list_remove(&device->source_resource_listener.link);
   }

   if ((device->source_resource = source_resource))
      wl_resource_add_destroy_listener(source_resource, &device->source_resource_listener);

   wlc_data_device_offer(device, wl_client);
}

static struct wl_data_device_interface wl_data_device_implementation = {
   .start_drag = wl_cb_data_device_start_drag,
   .set_selection = wl_cb_data_device_set_selection
};

static void
wl_cb_data_device_destructor(struct wl_resource *device_resource)
{
   wl_list_remove(wl_resource_get_link(device_resource));
}

static void
wl_cb_manager_get_data_device(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id, struct wl_resource *seat_resource)
{
   struct wlc_seat *seat;
   if (!(seat = wl_resource_get_user_data(seat_resource)))
      return;

   struct wl_resource *device_resource;
   if (!(device_resource = wl_resource_create(wl_client, &wl_data_device_interface, wl_resource_get_version(resource), id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   wl_resource_set_implementation(device_resource, &wl_data_device_implementation, seat->device, wl_cb_data_device_destructor);
   wl_list_insert(&seat->device->resources, wl_resource_get_link(device_resource));
}

static const struct wl_data_device_manager_interface wl_data_device_manager_implementation = {
   .create_data_source = wl_cb_manager_create_data_source,
   .get_data_device = wl_cb_manager_get_data_device
};

static void
wl_data_device_manager_bind(struct wl_client *wl_client, void *data, unsigned int version, unsigned int id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create(wl_client, &wl_data_device_manager_interface, MIN(version, 1), id))) {
      wl_client_post_no_memory(wl_client);
      wlc_log(WLC_LOG_WARN, "Failed create resource or bad version (%u > %u)", version, 1);
      return;
   }

   wl_resource_set_implementation(resource, &wl_data_device_manager_implementation, data, NULL);
}

void
wlc_data_device_offer(struct wlc_data_device *device, struct wl_client *wl_client)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_find_for_client(&device->resources, wl_client)))
      return;

   struct wlc_data_source *source = (device->source_resource ? wl_resource_get_user_data(device->source_resource) : NULL);
   struct wl_resource *offer = NULL;
   if (source && !(offer = wl_resource_create(wl_client, &wl_data_offer_interface, wl_resource_get_version(resource), 0)))
      return;

   if (offer) {
      wl_resource_set_implementation(offer, &wl_data_offer_implementation, source, &wl_cb_data_offer_destructor);
      wl_list_insert(&source->offers, wl_resource_get_link(offer));

      wl_data_device_send_data_offer(resource, offer);

      if (offer && source) {
         struct wlc_string *type;
         wl_array_for_each(type, &source->types)
            wl_data_offer_send_offer(offer, type->data);
      }
   }

   wl_data_device_send_selection(resource, offer);
}

static void
wlc_cb_data_device_source_destructor(struct wl_listener *listener, void *data)
{
   (void)data;
   struct wlc_data_device *device = wl_container_of(listener, device, source_resource_listener);
   device->source_resource = NULL;
}

struct wlc_data_device*
wlc_data_device_new(void)
{
   struct wlc_data_device *device;
   if (!(device = calloc(1, sizeof(struct wlc_data_device))))
      return NULL;

   wl_list_init(&device->resources);
   device->source_resource_listener.notify = &wlc_cb_data_device_source_destructor;
   return device;
}

void
wlc_data_device_free(struct wlc_data_device *device)
{
   assert(device);

   struct wl_resource *resource, *rn;
   wl_resource_for_each_safe(resource, rn, &device->resources)
      wl_resource_destroy(resource);

   free(device);
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
   wlc_log(WLC_LOG_WARN, "Out of memory");
   goto fail;
manager_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind data device manager interface");
fail:
   if (manager)
      wlc_data_device_manager_free(manager);
   return NULL;
}
