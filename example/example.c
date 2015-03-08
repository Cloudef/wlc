#include <stdlib.h>
#include <wlc/wlc.h>
#include <chck/math/math.h>

static wlc_handle
get_topmost(wlc_handle output, size_t offset)
{
   size_t memb;
   const wlc_handle *views;
   if ((views = wlc_output_get_views(output, &memb)) && memb > offset)
      return views[memb - (1 + offset)];
   return 0;
}

static void
relayout(wlc_handle output)
{
   const struct wlc_size *r;
   if (!(r = wlc_output_get_resolution(output)))
      return;

   size_t memb;
   const wlc_handle *views = wlc_output_get_views(output, &memb);

   bool toggle = false;
   uint32_t y = 0;
   uint32_t w = r->w / 2, h = r->h / chck_maxu32((1 + memb) / 2, 1);
   for (size_t i = 0; i < memb; ++i) {
      struct wlc_geometry g = { { (toggle ? w : 0), y }, { (!toggle && i == memb - 1 ? r->w : w), h } };
      wlc_view_set_geometry(views[i], &g);
      y = y + (!(toggle = !toggle) ? h : 0);
   }
}

static bool
output_created(wlc_handle output)
{
   printf("created output (%zu)\n", output);
   return true;
}

static void
output_destroyed(wlc_handle output)
{
   printf("destroyed output (%zu)\n", output);
}

static void
output_resolution(wlc_handle output, const struct wlc_size *from, const struct wlc_size *to)
{
   (void)from, (void)to;
   relayout(output);
}

static bool
view_created(wlc_handle view)
{
   printf("created view (%zu)\n", view);
   wlc_view_bring_to_front(view);
   wlc_view_focus(view);
   relayout(wlc_view_get_output(view));
   return true;
}

static void
view_destroyed(wlc_handle view)
{
   printf("destroyed view (%zu)\n", view);
   wlc_view_focus(get_topmost(wlc_view_get_output(view), 0));
   relayout(wlc_view_get_output(view));
}

static void
view_focus(wlc_handle view, bool focus)
{
   wlc_view_set_state(view, WLC_BIT_ACTIVATED, focus);
}

static bool
keyboard_key(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t key, uint32_t sym, enum wlc_key_state state)
{
   (void)time, (void)key;

   if (view && state == WLC_KEY_STATE_PRESSED) {
      if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_q) {
         wlc_view_close(view);
         return false;
      } else if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_Down) {
         wlc_view_send_to_back(view);
         wlc_view_focus(get_topmost(wlc_view_get_output(view), 0));
         return false;
      }
   }

   return true;
}

static bool
pointer_button(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t button, enum wlc_button_state state)
{
   (void)button, (void)time, (void)modifiers;

   if (state == WLC_BUTTON_STATE_PRESSED)
      wlc_view_focus(view);

   return true;
}

int
main(int argc, char *argv[])
{
   static struct wlc_interface interface = {
      .output = {
         .created = output_created,
         .destroyed = output_destroyed,
         .resolution = output_resolution,
      },

      .view = {
         .created = view_created,
         .destroyed = view_destroyed,
         .focus = view_focus,
      },

      .keyboard = {
         .key = keyboard_key,
      },

      .pointer = {
         .button = pointer_button,
      },
   };

   if (!wlc_init(&interface, argc, argv))
      return EXIT_FAILURE;

   wlc_run();
   return EXIT_SUCCESS;
}
