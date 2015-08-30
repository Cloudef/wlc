#include "client.h"

static struct compositor_test compositor;
static bool fullscreen_state_requested = false;
static bool view_moved_to_output = false;

static int
client_main(void)
{
   struct client_test client;
   client_test_create(&client, "fullscreen", 320, 320);
   surface_create(&client);
   shell_surface_create(&client);
   client_test_roundtrip(&client);
   client_test_roundtrip(&client);

   struct output *o;
   assert((o = chck_iter_pool_get(&client.outputs, 0)));
   wl_shell_surface_set_fullscreen(client.view.ssurface, 0, 0, o->output);
   wl_surface_commit(client.view.surface);
   while (wl_display_dispatch(client.display) != -1);
   return client_test_end(&client);
}

static void
view_move_to_output(wlc_handle view, wlc_handle from_output, wlc_handle to_output)
{
   (void)view;
   assert(from_output != to_output);
   view_moved_to_output = true;

   if (fullscreen_state_requested)
      signal_client(&compositor);

}

static void
view_request_state(wlc_handle view, enum wlc_view_state_bit state, bool toggle)
{
   (void)view;
   if (state == WLC_BIT_FULLSCREEN && toggle)
      fullscreen_state_requested = true;

   wlc_view_set_state(view, state, toggle);

   if (view_moved_to_output)
      signal_client(&compositor);
}

static void
compositor_ready(void)
{
   compositor_test_fork_client(&compositor, client_main);
}

static int
compositor_main(int argc, char *argv[])
{
   static struct wlc_interface interface = {
      .view = {
         .move_to_output = view_move_to_output,

         .request = {
            .state = view_request_state,
         },
      },

      .compositor = {
         .ready = compositor_ready,
      },
   };

   // This causes nouveau to hang at quit.
   // Comment out until headless backend is added.
   // setenv("WLC_OUTPUTS", "2", true);
   view_moved_to_output = true;

   compositor_test_create(&compositor, argc, argv, "fullscreen", &interface);
   wlc_run();

   assert(fullscreen_state_requested);
   assert(view_moved_to_output);
   return compositor_test_end(&compositor);
}

int
main(int argc, char *argv[])
{
   return compositor_main(argc, argv);
}
