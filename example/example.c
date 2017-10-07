#include <stdlib.h>
#include <stdio.h>
#include <wlc/wlc.h>
#include <chck/math/math.h>
#include <linux/input.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static struct {
   struct {
      wlc_handle view;
      struct wlc_point grab;
      uint32_t edges;
   } action;
} compositor;

static bool
start_interactive_action(wlc_handle view, const struct wlc_point *origin)
{
   if (compositor.action.view)
      return false;

   compositor.action.view = view;
   compositor.action.grab = *origin;
   wlc_view_bring_to_front(view);
   return true;
}

static void
start_interactive_move(wlc_handle view, const struct wlc_point *origin)
{
   start_interactive_action(view, origin);
}

static void
start_interactive_resize(wlc_handle view, uint32_t edges, const struct wlc_point *origin)
{
   const struct wlc_geometry *g;
   if (!(g = wlc_view_get_geometry(view)) || !start_interactive_action(view, origin))
      return;

   const int32_t halfw = g->origin.x + g->size.w / 2;
   const int32_t halfh = g->origin.y + g->size.h / 2;

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
   // very simple layout function
   // you probably don't want to layout certain type of windows in wm

   const struct wlc_size *r;
   if (!(r = wlc_output_get_virtual_resolution(output)))
      return;

   size_t memb;
   const wlc_handle *views = wlc_output_get_views(output, &memb);

   size_t positioned = 0;
   for (size_t i = 0; i < memb; ++i)
      if (wlc_view_positioner_get_anchor_rect(views[i]) == NULL)
         positioned ++;

   bool toggle = false;
   uint32_t y = 0;
   const uint32_t n = chck_maxu32((1 + positioned) / 2, 1);
   const uint32_t w = r->w / 2, h = r->h / n;
   const uint32_t ew = r->w - w * 2, eh = r->h - h * n;
   size_t j = 0;
   for (size_t i = 0; i < memb; ++i) {
      const struct wlc_geometry* anchor_rect = wlc_view_positioner_get_anchor_rect(views[i]);
      if (anchor_rect == NULL) {
         const struct wlc_geometry g = {
            .origin = {
               .x = (toggle ? w + ew : 0),
               .y =  y
            },
            .size = {
               .w = (!toggle && j == positioned - 1 ? r->w : (toggle ? w : w + ew)),
               .h = (j < 2 ? h + eh : h)
            }
         };
         wlc_view_set_geometry(views[i], 0, &g);
         y = y + (!(toggle = !toggle) ? g.size.h : 0);
         j ++;
      } else {
         struct wlc_size size_req = *wlc_view_positioner_get_size(views[i]);
         if ((size_req.w <= 0) || (size_req.h <= 0)) {
             const struct wlc_geometry* current = wlc_view_get_geometry(views[i]);
             size_req = current->size;
         }
         struct wlc_geometry g = {
            .origin = anchor_rect->origin,
            .size = size_req
         };
         wlc_handle parent = wlc_view_get_parent(views[i]);
         if (parent) {
            const struct wlc_geometry* parent_geometry = wlc_view_get_geometry(parent);
            g.origin.x += parent_geometry->origin.x;
            g.origin.y += parent_geometry->origin.y;
         }
         wlc_view_set_geometry(views[i], 0, &g);
     }
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
   wlc_view_set_mask(view, wlc_output_get_mask(wlc_view_get_output(view)));
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
view_request_move(wlc_handle view, const struct wlc_point *origin)
{
   start_interactive_move(view, origin);
}

static void
view_request_resize(wlc_handle view, uint32_t edges, const struct wlc_point *origin)
{
   start_interactive_resize(view, edges, origin);
}

static void
view_request_geometry(wlc_handle view, const struct wlc_geometry *g)
{
   (void)view, (void)g;
   // stub intentionally to ignore geometry requests.
}

static int
cb_selection_data(int fd, uint32_t mask, void *data)
{
   ((void) data);
   struct wlc_event_source **sourceptr = (struct wlc_event_source**) data;

   if (!sourceptr || !(*sourceptr)) {
      printf("error: selection cb, no src pointer\n");
      return 0;
   }

   if (mask & WLC_EVENT_ERROR) {
      printf("selection data fd error\n");
      goto cleanup;
   }

   if (mask & WLC_EVENT_READABLE) {
      char buf[512];
      int ret = read(fd, buf, 511);
      if (ret < 0) {
         printf("reading from selection fd failed: %s\n", strerror(errno));
         goto cleanup;
      }

      buf[ret] = '\0';
      printf("Received clipboard data: %s\n", buf);
   }

cleanup:
   wlc_event_source_remove(*sourceptr);
   close(fd);
   *sourceptr = NULL;
   return 0;
}

static bool
keyboard_key(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t key, enum wlc_key_state state)
{
   (void)time, (void)key;
   const uint32_t sym = wlc_keyboard_get_keysym_for_key(key, NULL);

   if (view) {
      if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_q) {
         if (state == WLC_KEY_STATE_PRESSED) {
            wlc_view_close(view);
         }
         return true;
      } else if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_Down) {
         if (state == WLC_KEY_STATE_PRESSED) {
            wlc_view_send_to_back(view);
            wlc_view_focus(get_topmost(wlc_view_get_output(view), 0));
         }
         return true;
      }
   }

   if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_Escape) {
      if (state == WLC_KEY_STATE_PRESSED) {
         wlc_terminate();
      }
      return true;
   } else if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_Return) {
      if (state == WLC_KEY_STATE_PRESSED) {
         char *terminal = (getenv("TERMINAL") ? getenv("TERMINAL") : "weston-terminal");
         wlc_exec(terminal, (char *const[]){ terminal, NULL });
      }
      return true;
   } else if (modifiers->mods & WLC_BIT_MOD_CTRL && sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
      if (state == WLC_KEY_STATE_PRESSED) {
         size_t memb;
         const wlc_handle *outputs = wlc_get_outputs(&memb);
         const uint32_t scale = (sym - XKB_KEY_1) + 1;

         for (size_t i = 0; i < memb; ++i)
            wlc_output_set_resolution(outputs[i], wlc_output_get_resolution(outputs[i]), scale);

         printf("scale: %u\n", scale);
      }
      return true;
   } else if (modifiers->mods & WLC_BIT_MOD_CTRL && sym == XKB_KEY_comma && state == WLC_KEY_STATE_PRESSED) {
      size_t size;
      const char **types = wlc_get_selection_types(&size);
      for (size_t i = 0; i < size; ++i) {
         if (strcmp(types[i], "text/plain;charset=utf-8") == 0 || strcmp(types[i], "text/plain") == 0) {
            int pipes[2];
            if (pipe(pipes) == -1) {
               printf("pipe failed: %s\n", strerror(errno));
               break;
            }

            fcntl(pipes[0], F_SETFD, O_CLOEXEC | O_NONBLOCK);
            fcntl(pipes[1], F_SETFD, O_CLOEXEC | O_NONBLOCK);
            if (!wlc_get_selection_data(types[i], pipes[1])) {
               close(pipes[0]);
               close(pipes[1]);
               printf("error: get selection data failed for valid selection\n");
               break;
            }

            static struct wlc_event_source *src = NULL;
            static int recv_fd = -1;
            if (src) {
               wlc_event_source_remove(src);
               close(recv_fd);
            }

            src = wlc_event_loop_add_fd(pipes[0], WLC_EVENT_READABLE | WLC_EVENT_ERROR | WLC_EVENT_HANGUP, cb_selection_data, &src);
            recv_fd = pipes[0];
            break;
         }
      }

      free(types);
   }

   return false;
}

static bool
pointer_button(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t button, enum wlc_button_state state, const struct wlc_point *position)
{
   (void)button, (void)time, (void)modifiers;

   if (state == WLC_BUTTON_STATE_PRESSED) {
      wlc_view_focus(view);
      if (view) {
         if (modifiers->mods & WLC_BIT_MOD_CTRL && button == BTN_LEFT)
            start_interactive_move(view, position);
         if (modifiers->mods & WLC_BIT_MOD_CTRL && button == BTN_RIGHT)
            start_interactive_resize(view, 0, position);
      }
   } else {
      stop_interactive_action();
   }

   return (compositor.action.view ? true : false);
}

static bool
pointer_motion(wlc_handle handle, uint32_t time, double x, double y)
{
   (void)handle, (void)time;

   if (compositor.action.view) {
      const int32_t dx = x - compositor.action.grab.x;
      const int32_t dy = y - compositor.action.grab.y;
      struct wlc_geometry g = *wlc_view_get_geometry(compositor.action.view);

      if (compositor.action.edges) {
         const struct wlc_size min = { 80, 40 };

         struct wlc_geometry n = g;
         if (compositor.action.edges & WLC_RESIZE_EDGE_LEFT) {
            n.size.w -= dx;
            n.origin.x += dx;
         } else if (compositor.action.edges & WLC_RESIZE_EDGE_RIGHT) {
            n.size.w += dx;
         }

         if (compositor.action.edges & WLC_RESIZE_EDGE_TOP) {
            n.size.h -= dy;
            n.origin.y += dy;
         } else if (compositor.action.edges & WLC_RESIZE_EDGE_BOTTOM) {
            n.size.h += dy;
         }

         if (n.size.w >= min.w) {
            g.origin.x = n.origin.x;
            g.size.w = n.size.w;
         }

         if (n.size.h >= min.h) {
            g.origin.y = n.origin.y;
            g.size.h = n.size.h;
         }

         wlc_view_set_geometry(compositor.action.view, compositor.action.edges, &g);
      } else {
         g.origin.x += dx;
         g.origin.y += dy;
         wlc_view_set_geometry(compositor.action.view, 0, &g);
      }

      compositor.action.grab.x = x;
      compositor.action.grab.y = y;
   }

   // In order to give the compositor control of the pointer placement it needs
   // to be explicitly set after receiving the motion event:
   wlc_pointer_set_position_v2(x, y);
   return (compositor.action.view ? true : false);
}

static void
cb_data_source(void *data, const char *type, int fd)
{
   ((void) type);
   const char *str = data;
   write(fd, str, strlen(str));
   close(fd);
}

static void
cb_log(enum wlc_log_type type, const char *str)
{
   (void)type;
   printf("%s\n", str);
}

int
main(void)
{
   wlc_log_set_handler(cb_log);

   wlc_set_output_resolution_cb(output_resolution);
   wlc_set_view_created_cb(view_created);
   wlc_set_view_destroyed_cb(view_destroyed);
   wlc_set_view_focus_cb(view_focus);
   wlc_set_view_request_move_cb(view_request_move);
   wlc_set_view_request_resize_cb(view_request_resize);
   wlc_set_view_request_geometry_cb(view_request_geometry);
   wlc_set_keyboard_key_cb(keyboard_key);
   wlc_set_pointer_button_cb(pointer_button);
   wlc_set_pointer_motion_cb_v2(pointer_motion);

   if (!wlc_init())
      return EXIT_FAILURE;

   const char *type = "text/plain;charset=utf-8";
   wlc_set_selection("wlc", &type, 1, &cb_data_source);

   wlc_run();
   return EXIT_SUCCESS;
}
