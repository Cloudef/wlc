#ifndef _WLC_H_
#define _WLC_H_

#include <stdbool.h>
#include <stdint.h>

struct wlc_compositor;
struct wlc_view;

enum wlc_key_state {
   WLC_KEY_STATE_RELEASED = 0,
   WLC_KEY_STATE_PRESSED = 1
};

enum wlc_button_state {
   WLC_BUTTON_STATE_RELEASED = 0,
   WLC_BUTTON_STATE_PRESSED = 1
};

enum wlc_view_state {
   WLC_MAXIMIZED = 1,
   WLC_FULLSCREEN = 2,
   WLC_RESIZING = 3,
   WLC_ACTIVATED = 4,
};

struct wlc_interface {
   struct {
      void (*created)(struct wlc_compositor*, struct wlc_view*);
      void (*destroyed)(struct wlc_compositor*, struct wlc_view*);
   } view;

   struct {
      bool (*key)(struct wlc_compositor*, struct wlc_view*, uint32_t key, enum wlc_key_state state);
   } keyboard;

   struct {
      bool (*button)(struct wlc_compositor*, struct wlc_view*, uint32_t button, enum wlc_button_state state);
      bool (*motion)(struct wlc_compositor*, struct wlc_view*, int32_t x, int32_t y);
   } pointer;
};

void wlc_view_set_state(struct wlc_view *view, const uint32_t *states, uint32_t memb);
void wlc_view_resize(struct wlc_view *view, uint32_t width, uint32_t height);
void wlc_view_position(struct wlc_view *view, int32_t x, int32_t y);

void wlc_compositor_keyboard_focus(struct wlc_compositor *compositor, struct wlc_view *view);

void wlc_compositor_inject(struct wlc_compositor *compositor, const struct wlc_interface *interface);
void wlc_compositor_run(struct wlc_compositor *compositor);
void wlc_compositor_free(struct wlc_compositor *compositor);
struct wlc_compositor* wlc_compositor_new(void);

#endif /* _WLC_H_ */
