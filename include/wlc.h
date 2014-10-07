#ifndef _WLC_H_
#define _WLC_H_

#include <stdbool.h>
#include <stdint.h>

#define wlc_view_for_each(view, list)                                \
        for (view = 0, view = wlc_view_from_link((list)->next);      \
             wlc_view_get_link(view) != (list);                      \
             view = wlc_view_from_link(wlc_view_get_link(view)->next))

#define wl_view_for_each_safe(view, tmp, list)                      \
        for (view = 0, tmp = 0,                                     \
             view = wlc_view_from_link((list)->next),               \
             tmp = wlc_view_from_link((list)->next->next);          \
             wlc_view_get_link(view) != (list);                     \
             view = tmp,                                            \
             tmp = wlc_view_from_link(wlc_view_get_link(view)->next))

struct wlc_compositor;
struct wlc_view;
struct wlc_output;
struct wl_list;

enum wlc_modifier_bit {
   WLC_BIT_MOD_SHIFT = 1<<0,
   WLC_BIT_MOD_CAPS = 1<<1,
   WLC_BIT_MOD_CTRL = 1<<2,
   WLC_BIT_MOD_ALT = 1<<3,
   WLC_BIT_MOD_MOD2 = 1<<4,
   WLC_BIT_MOD_MOD3 = 1<<5,
   WLC_BIT_MOD_LOGO = 1<<6,
   WLC_BIT_MOD_MOD5 = 1<<7,
};

enum wlc_led_bit {
   WLC_BIT_LED_NUM = 1<<0,
   WLC_BIT_LED_CAPS = 1<<1,
   WLC_BIT_LED_SCROLL = 1<<2,
};

enum wlc_key_state {
   WLC_KEY_STATE_RELEASED = 0,
   WLC_KEY_STATE_PRESSED = 1
};

enum wlc_button_state {
   WLC_BUTTON_STATE_RELEASED = 0,
   WLC_BUTTON_STATE_PRESSED = 1
};

struct wlc_interface {
   struct {
      void (*created)(struct wlc_compositor*, struct wlc_view*);
      void (*destroyed)(struct wlc_compositor*, struct wlc_view*);
      void (*move)(struct wlc_compositor*, struct wlc_view*, int32_t x, int32_t y);
      void (*resize)(struct wlc_compositor*, struct wlc_view*, uint32_t width, uint32_t height);
   } view;

   struct {
      void (*init)(struct wlc_compositor*, struct wlc_view*);
      bool (*key)(struct wlc_compositor*, struct wlc_view*, uint32_t leds, uint32_t mods, uint32_t key, enum wlc_key_state state);
   } keyboard;

   struct {
      bool (*button)(struct wlc_compositor*, struct wlc_view*, uint32_t button, enum wlc_button_state state);
      bool (*motion)(struct wlc_compositor*, struct wlc_view*, int32_t x, int32_t y);
   } pointer;

   struct {
      void (*created)(struct wlc_compositor*, struct wlc_output*);
      void (*destroyed)(struct wlc_compositor*, struct wlc_output*);
      void (*resolution)(struct wlc_compositor*, uint32_t width, uint32_t height);
   } output;
};

bool wlc_init(void);

void wlc_view_set_maximized(struct wlc_view *view, bool maximized);
void wlc_view_set_fullscreen(struct wlc_view *view, bool fullscreen);
void wlc_view_set_resizing(struct wlc_view *view, bool resizing);
void wlc_view_set_active(struct wlc_view *view, bool active);
void wlc_view_resize(struct wlc_view *view, uint32_t width, uint32_t height);
void wlc_view_position(struct wlc_view *view, int32_t x, int32_t y);
void wlc_view_close(struct wlc_view *view);
void wlc_view_send_below(struct wlc_view *view, struct wlc_view *below);
void wlc_view_send_to_back(struct wlc_view *view);
void wlc_view_bring_above(struct wlc_view *view, struct wlc_view *above);
void wlc_view_bring_to_front(struct wlc_view *view);
struct wl_list* wlc_view_get_link(struct wlc_view *view);
struct wlc_view* wlc_view_from_link(struct wl_list *view_link);

void wlc_compositor_keyboard_focus(struct wlc_compositor *compositor, struct wlc_view *view);

void wlc_compositor_inject(struct wlc_compositor *compositor, const struct wlc_interface *interface);
void wlc_compositor_run(struct wlc_compositor *compositor);
void wlc_compositor_free(struct wlc_compositor *compositor);
struct wlc_compositor* wlc_compositor_new(void);

#endif /* _WLC_H_ */
