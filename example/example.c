#include <stdlib.h>
#include <wlc/wlc.h>
#include <chck/math/math.h>
#include <linux/input.h>

static struct {
   struct {
      wlc_handle view;
      struct wlc_origin grab;
      struct wlc_geometry geometry;
      uint32_t edges;
   } action;
} compositor;

static bool
start_interactive_action(wlc_handle view, const struct wlc_origin *origin)
{
   const struct wlc_geometry *g;
   if (compositor.action.view || !(g = wlc_view_get_geometry(view)))
      return false;

   compositor.action.view = view;
   compositor.action.grab = *origin;
   compositor.action.geometry = *g;
   return true;
}

static void
start_interactive_move(wlc_handle view, const struct wlc_origin *origin)
{
   start_interactive_action(view, origin);
}

static void
start_interactive_resize(wlc_handle view, uint32_t edges, const struct wlc_origin *origin)
{
   if (!start_interactive_action(view, origin))
      return;

   const int32_t halfw = compositor.action.geometry.origin.x + compositor.action.geometry.size.w / 2;
   const int32_t halfh = compositor.action.geometry.origin.y + compositor.action.geometry.size.h / 2;

   if (!(compositor.action.edges = edges)) {
      compositor.action.edges = (origin->x < halfw ? WLC_RESIZE_EDGE_LEFT : (origin->x > halfw ? WLC_RESIZE_EDGE_RIGHT : 0)) |
                                (origin->y < halfh ? WLC_RESIZE_EDGE_TOP : (origin->y > halfh ? WLC_RESIZE_EDGE_BOTTOM : 0));
   }

   wlc_view_set_state(view, WLC_BIT_RESIZING, true);
}

static void
stop_interactive_action(void)
{
   if (!compositor.action.view)
      return;

   wlc_view_set_state(compositor.action.view, WLC_BIT_RESIZING, false);
   memset(&compositor.action, 0, sizeof(compositor.action));
}

static wlc_handle
get_topmost(wlc_handle output, size_t offset)
{
   size_t memb;
   const wlc_handle *views = wlc_output_get_views(output, &memb);
   return (memb > 0 ? views[(memb - 1 + offset) % memb] : 0);
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
      wlc_view_set_geometry(views[i], 0, &g);
      y = y + (!(toggle = !toggle) ? h : 0);
   }
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
   wlc_view_bring_to_front(view);
   wlc_view_focus(view);
   relayout(wlc_view_get_output(view));
   return true;
}

static void
view_destroyed(wlc_handle view)
{
   wlc_view_focus(get_topmost(wlc_view_get_output(view), 0));
   relayout(wlc_view_get_output(view));
}

static void
view_focus(wlc_handle view, bool focus)
{
   wlc_view_set_state(view, WLC_BIT_ACTIVATED, focus);
}

static void
view_request_move(wlc_handle view, const struct wlc_origin *origin)
{
   start_interactive_move(view, origin);
}

static void
view_request_resize(wlc_handle view, uint32_t edges, const struct wlc_origin *origin)
{
   start_interactive_resize(view, edges, origin);
}

static bool
keyboard_key(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t key, uint32_t sym, enum wlc_key_state state)
{
   (void)time, (void)key;

   if (state == WLC_KEY_STATE_PRESSED) {
      if (view) {
         if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_q) {
            wlc_view_close(view);
            return true;
         } else if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_Down) {
            wlc_view_send_to_back(view);
            wlc_view_focus(get_topmost(wlc_view_get_output(view), 0));
            return true;
         }
      }

      if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_Escape) {
         wlc_terminate();
         return true;
      } else if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_Return) {
         char *terminal = (getenv("TERMINAL") ? getenv("TERMINAL") : "weston-terminal");
         wlc_exec(terminal, (char *const[]){ terminal, NULL });
         return true;
      }
   }

   return false;
}

static bool
pointer_button(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t button, enum wlc_button_state state, const struct wlc_origin *origin)
{
   (void)button, (void)time, (void)modifiers;

   if (state == WLC_BUTTON_STATE_PRESSED) {
      wlc_view_focus(view);
      if (view) {
         if (modifiers->mods & WLC_BIT_MOD_CTRL && button == BTN_LEFT)
            start_interactive_move(view, origin);
         if (modifiers->mods & WLC_BIT_MOD_CTRL && button == BTN_RIGHT)
            start_interactive_resize(view, 0, origin);
      }
   } else {
      stop_interactive_action();
   }

   return (compositor.action.view ? true : false);
}

static bool
pointer_motion(wlc_handle handle, uint32_t time, const struct wlc_origin *origin)
{
   (void)handle, (void)time;

   if (compositor.action.view) {
      struct wlc_geometry *g = &compositor.action.geometry;

      if (compositor.action.edges) {
         const struct wlc_size min = { 80, 40 };
         const int32_t wd = chck_max32(0, origin->x - g->origin.x);
         const int32_t hd = chck_max32(0, origin->y - g->origin.y);

         if (compositor.action.edges & WLC_RESIZE_EDGE_LEFT) {
            const uint32_t tw = chck_max32(0, g->size.w - (origin->x - g->origin.x));
            g->size.w = chck_maxu32(min.w, tw);
            g->origin.x = (g->size.w > min.w ? origin->x : g->origin.x);
         } else if (compositor.action.edges & WLC_RESIZE_EDGE_RIGHT) {
            g->size.w = chck_maxu32(min.w, wd);
         }

         if (compositor.action.edges & WLC_RESIZE_EDGE_TOP) {
            const uint32_t th = chck_max32(0, g->size.h - (origin->y - g->origin.y));
            g->size.h = chck_maxu32(min.h, th);
            g->origin.y = (g->size.h > min.h ? origin->y : g->origin.y);
         } else if (compositor.action.edges & WLC_RESIZE_EDGE_BOTTOM) {
            g->size.h = chck_maxu32(min.h, hd);
         }

         const struct wlc_geometry *c;
         if ((c = wlc_view_get_geometry(compositor.action.view))) {
            const struct wlc_geometry r = { g->origin, g->size };
            wlc_view_set_geometry(compositor.action.view, compositor.action.edges, &r);
         }
      } else {
         const int32_t dx = origin->x - compositor.action.grab.x;
         const int32_t dy = origin->y - compositor.action.grab.y;

         g->origin.x += dx;
         g->origin.y += dy;

         wlc_view_set_geometry(compositor.action.view, 0, g);
      }

      compositor.action.grab = *origin;
   }

   return (compositor.action.view ? true : false);
}

int
main(int argc, char *argv[])
{
   static struct wlc_interface interface = {
      .output = {
         .resolution = output_resolution,
      },

      .view = {
         .created = view_created,
         .destroyed = view_destroyed,
         .focus = view_focus,

         .request = {
            .move = view_request_move,
            .resize = view_request_resize,
         },
      },

      .keyboard = {
         .key = keyboard_key,
      },

      .pointer = {
         .button = pointer_button,
         .motion = pointer_motion,
      },
   };

   if (!wlc_init(&interface, argc, argv))
      return EXIT_FAILURE;

   wlc_run();
   return EXIT_SUCCESS;
}
