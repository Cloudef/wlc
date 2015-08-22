#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <wayland-server.h>
#include <chck/string/string.h>
#include "internal.h"
#include "macros.h"
#include "data.h"
#include "seat.h"
#include "resources/types/data-source.h"

static void
wl_cb_data_offer_accept(struct wl_client *client, struct wl_resource *resource, uint32_t serial, const char *type)
{
   (void)client, (void)serial;

   wlc_resource source;
   if (!(source = (wlc_resource)wl_resource_get_user_data(resource)))
      return;

   wl_data_source_send_target(wl_resource_from_wlc_resource(source, "data-source"), type);
}

static void
wl_cb_data_offer_receive(struct wl_client *client, struct wl_resource *resource, const char *type, int fd)
{
   (void)client;

   wlc_resource source;
   if (!(source = (wlc_resource)wl_resource_get_user_data(resource)))
      return;

   wl_data_source_send_send(wl_resource_from_wlc_resource(source, "data-source"), type, fd);
   close(fd);
}

static struct wl_data_offer_interface wl_data_offer_implementation = {
   .accept = wl_cb_data_offer_accept,
   .receive = wl_cb_data_offer_receive,
   .destroy = wlc_cb_resource_destructor
};

static void
wl_cb_data_source_offer(struct wl_client *client, struct wl_resource *resource, const char *type)
{
   (void)client, (void)resource, (void)type;

   struct wlc_data_source *source;
   if (!(source = convert_from_wl_resource(resource, "data-source")))
      return;

   struct chck_string *destination;
   if (!(destination = chck_iter_pool_push_back(&source->types, NULL)))
      return;

   chck_string_set_cstr(destination, type, true);
}

static struct wl_data_source_interface wl_data_source_implementation = {
   .offer = wl_cb_data_source_offer,
   .destroy = wlc_cb_resource_destructor
};

static void
wl_cb_manager_create_data_source(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wlc_data_device_manager *manager;
   if (!(manager = wl_resource_get_user_data(resource)))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&manager->sources, client, &wl_data_source_interface, wl_resource_get_version(resource), 2, id)))
      return;

   wlc_resource_implement(r, &wl_data_source_implementation, manager);
}

static void
wl_cb_data_device_start_drag(struct wl_client *client, struct wl_resource *resource, struct wl_resource *source_resource, struct wl_resource *origin_resource, struct wl_resource *icon_resource, uint32_t serial)
{
   (void)client, (void)resource, (void)source_resource, (void)origin_resource, (void)icon_resource, (void)serial;
   STUBL(resource);
}

static void
wl_cb_data_device_set_selection(struct wl_client *client, struct wl_resource *resource, struct wl_resource *source_resource, uint32_t serial)
{
   (void)serial;

   struct wlc_data_device_manager *manager;
   if (!(manager = wl_resource_get_user_data(resource)))
      return;

   struct wl_resource *current = wl_resource_from_wlc_resource(manager->source, "data-source");
   if (source_resource == current)
      return;

   if (current)
      wl_data_source_send_cancelled(current);

   manager->source = wlc_resource_from_wl_resource(source_resource);
   wlc_data_device_manager_offer(manager, client);
}

static struct wl_data_device_interface wl_data_device_implementation = {
   .start_drag = wl_cb_data_device_start_drag,
   .set_selection = wl_cb_data_device_set_selection,
   .release = wlc_cb_resource_destructor
};

static void
wl_cb_manager_get_data_device(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *seat_resource)
{
   struct wlc_seat *seat;
   if (!(seat = wl_resource_get_user_data(seat_resource)))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&seat->manager.devices, client, &wl_data_device_interface, wl_resource_get_version(resource), 2, id)))
      return;

   wlc_resource_implement(r, &wl_data_device_implementation, &seat->manager);
}

static const struct wl_data_device_manager_interface wl_data_device_manager_implementation = {
   .create_data_source = wl_cb_manager_create_data_source,
   .get_data_device = wl_cb_manager_get_data_device
};

static void
wl_data_device_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create_checked(client, &wl_data_device_manager_interface, version, 2, id)))
      return;

   wl_resource_set_implementation(resource, &wl_data_device_manager_implementation, data, NULL);
}

void
wlc_data_device_manager_offer(struct wlc_data_device_manager *manager, struct wl_client *client)
{
   assert(manager);

   struct wl_resource *resource;
   if (!client || !(resource = wl_resource_for_client(&manager->devices, client)))
      return;

   struct wlc_data_source *source = convert_from_wlc_resource(manager->source, "data-source");

   wlc_resource offer = 0;
   if (source && !(offer = wlc_resource_create(&manager->offers, client, &wl_data_offer_interface, wl_resource_get_version(resource), 2, 0)))
      return;

   if (offer) {
      wlc_resource_implement(offer, &wl_data_offer_implementation, (void*)manager->source);
      wl_data_device_send_data_offer(resource, wl_resource_from_wlc_resource(offer, "data-offer"));

      if (offer && source) {
         struct chck_string *type;
         chck_iter_pool_for_each(&source->types, type)
            wl_data_offer_send_offer(wl_resource_from_wlc_resource(offer, "data-offer"), type->data);
      }
   }

   wl_data_device_send_selection(resource, wl_resource_from_wlc_resource(offer, "data-offer"));
}

void
wlc_data_device_manager_release(struct wlc_data_device_manager *manager)
{
   if (!manager)
      return;

   if (manager->wl.manager)
      wl_global_destroy(manager->wl.manager);

   wlc_source_release(&manager->sources);
   wlc_source_release(&manager->devices);
   wlc_source_release(&manager->offers);

   memset(manager, 0, sizeof(struct wlc_data_device_manager));
}

bool
wlc_data_device_manager(struct wlc_data_device_manager *manager)
{
   assert(manager);
   memset(manager, 0, sizeof(struct wlc_data_device_manager));

   if (!(manager->wl.manager = wl_global_create(wlc_display(), &wl_data_device_manager_interface, 2, manager, wl_data_device_manager_bind)))
      goto manager_interface_fail;

   if (!wlc_source(&manager->sources, "data-source", wlc_data_source, wlc_data_source_release, 32, sizeof(struct wlc_data_source)) ||
       !wlc_source(&manager->devices, "data-device", NULL, NULL, 32, sizeof(struct wlc_resource)) ||
       !wlc_source(&manager->offers, "data-offer", NULL, NULL, 32, sizeof(struct wlc_resource)))
      goto fail;

   return true;

manager_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind data device manager interface");
fail:
   wlc_data_device_manager_release(manager);
   return false;
}
