#include "client.h"

int main(void)
{
   struct test test;
   test_create(&test, "fullscreen", 320, 320);
   surface_create(&test);
   shell_surface_create(&test);
   test_roundtrip(&test);

   struct output *o;
   assert((o = chck_iter_pool_get(&test.outputs, 0)));
   wl_shell_surface_set_fullscreen(test.view.ssurface, 0, 0, o->output);
   while (wl_display_dispatch(test.display) != -1);
}
