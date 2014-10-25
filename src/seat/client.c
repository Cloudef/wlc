#include "client.h"
#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>

struct wlc_client*
wlc_client_for_client_with_wl_client_in_list(struct wl_client *wl_client, struct wl_list *list)
{
   assert(wl_client && list);

   struct wlc_client *client;
   wl_list_for_each(client, list, link) {
      if (client->wl_client == wl_client)
         return client;
   }

   return NULL;
}

void
wlc_client_free(struct wlc_client *client)
{
   assert(client);

   if (client->wl_client) {
      wl_client_destroy(client->wl_client);
      return;
   }

   /* should wayland call destructor these automatically (?)
    * seems to crash without these.. investigate later. */
   for (int i = 0; i < WLC_INPUT_TYPE_LAST; ++i) {
      if (client->input[i])
         wl_resource_destroy(client->input[i]);
   }

   wl_list_remove(&client->link);
   free(client);
}

struct wlc_client*
wlc_client_new(struct wl_client *wl_client)
{
   assert(wl_client);

   struct wlc_client *client;
   if (!(client = calloc(1, sizeof(struct wlc_client))))
      goto fail;

   client->wl_client = wl_client;
   return client;

fail:
   if (client)
      wlc_client_free(client);
   return NULL;
}
