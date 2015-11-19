#include "wayland-test-extension-client-protocol.h"
#include "client.h"
#include <wlc/wlc-wayland.h>
#include "wayland-test-extension-server-protocol.h"

static struct compositor_test compositor;
static bool background_was_set = false;
static bool background_was_rendered = true;

static int
client_main(void)
{
   struct client_test client;
   client_test_create(&client, "wl-extension", 320, 320);
   surface_create(&client);
   client_test_roundtrip(&client);
   struct output *o;
   assert((o = chck_iter_pool_get(&client.outputs, 0)));
   background_set_background(client.background, o->output, client.view.surface);
   while (wl_display_dispatch(client.display) != -1);
   return client_test_end(&client);
}

static void
set_background(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output, struct wl_resource *surface)
{
   (void)client, (void)resource;
   assert(wlc_handle_from_wl_output_resource(output));
   assert(wlc_resource_from_wl_surface_resource(surface));
   wlc_handle_set_user_data(wlc_handle_from_wl_output_resource(output), (void*)wlc_resource_from_wl_surface_resource(surface));
   background_was_set = true;
}

static struct background_interface background_implementation = {
   .set_background = set_background,
};

static void
background_bind(struct wl_client *client, void *data, unsigned int version, unsigned int id)
{
   (void)data;

   if (version > 1) {
      // Unsupported version
      return;
   }

   struct wl_resource *resource;
   if (!(resource = wl_resource_create(client, &background_interface, version, id))) {
      wl_client_post_no_memory(client);
      return;
   }

   wl_resource_set_implementation(resource, &background_implementation, NULL, NULL);
}

static void
output_render_pre(wlc_handle output)
{
   wlc_resource surface;
   if (!(surface = (wlc_resource)wlc_handle_get_user_data(output)))
      return;

   wlc_surface_render(surface, &(struct wlc_geometry){ wlc_origin_zero, *wlc_output_get_resolution(output) });

   background_was_rendered = true;
   signal_client(&compositor);
}

static void
compositor_ready(void)
{
   // XXX: This is broken.
   // We can't use fork due to it inherting internal timerfd's
   // and causing havok. If we want to use fork for simplicity
   // we need to do that in main for both client / compositor
   // and signal with signalfd for example.
   compositor_test_fork_client(&compositor, client_main);
}

static int
compositor_main(int argc, char *argv[])
{
   static struct wlc_interface interface = {
      .output = {
         .render = {
            .pre = output_render_pre,
         },
      },

      .compositor = {
         .ready = compositor_ready,
      },
   };

   compositor_test_create(&compositor, argc, argv, "wl-extension", &interface);

   if (!wl_global_create(wlc_get_wl_display(), &background_interface, 1, NULL, background_bind))
      return EXIT_FAILURE;

   wlc_run();
   assert(background_was_set);
   return compositor_test_end(&compositor);
}

int
main(int argc, char *argv[])
{
   return compositor_main(argc, argv);
}
